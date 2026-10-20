// SPDX-License-Identifier: GPL-3.0-only

#include "completion/CodeGeneration.h"
#include "completion/CodeScan.h"
#include "completion/CompletionController.h"
#include "completion/CompletionHistory.h"
#include "completion/CompletionPopup.h"
#include "completion/CompletionService.h"
#include "completion/DocComments.h"
#include "completion/ExpectedType.h"
#include "completion/FullLine.h"
#include "completion/FuzzyMatch.h"
#include "completion/LocalCompletion.h"
#include "completion/NameSuggestions.h"
#include "completion/Snippet.h"
#include "completion/SnippetSession.h"
#include "completion/Templates.h"
#include "editor/EditorWidget.h"

#include <QAccessible>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListView>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QToolTip>
#include <QtTest>

namespace {

QStringList ranked(const QString &pattern, const QStringList &candidates)
{
    QList<std::pair<int, QString>> scored;
    for (const QString &candidate : candidates) {
        if (const auto match = FuzzyMatch::match(pattern, candidate))
            scored.append({match->score, candidate});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList result;
    for (const auto &[score, candidate] : scored)
        result.append(candidate);
    return result;
}

// A provider answering later, like a language server.
class LateProvider : public ICompletionProvider {
public:
    bool handles(const CompletionContext &) const override { return true; }

    void complete(const CompletionContext &, const Callback &done) override { pending = done; }

    QString triggerCharacters(const QString &) const override { return QStringLiteral("."); }

    Callback pending;
};

// Records what it is asked, and answers at once.
class RecordingProvider : public ICompletionProvider {
public:
    explicit RecordingProvider(QList<CompletionItem> answers, bool isSmart = false)
        : items(std::move(answers))
        , smart(isSmart)
    {
    }

    bool handles(const CompletionContext &) const override { return true; }

    bool servesSmart() const override { return smart; }

    void complete(const CompletionContext &context, const Callback &done) override
    {
        last = context;
        ++calls;
        done(items, false);
    }

    QList<CompletionItem> items;
    bool smart;
    CompletionContext last;
    int calls = 0;
};

// A Full Line model answering what it is told to.
class FakeFullLineModel : public FullLineModel {
public:
    void suggest(const QString &before, const QString &after, const QString &, Callback done) override
    {
        lastBefore = before;
        lastAfter = after;
        ++requests;
        done(lineOf(answer, after));
    }

    QString answer;
    QString lastBefore;
    QString lastAfter;
    int requests = 0;
};

} // namespace

class TestCompletion : public QObject {
    Q_OBJECT

private slots:

    void fuzzyMatchesWordStarts()
    {
        QVERIFY(FuzzyMatch::match("gsv", "getSelectedValue"));
        QVERIFY(FuzzyMatch::match("gsv", "get_saved_value"));
        QVERIFY(!FuzzyMatch::match("get", "widget")); // the first letter starts a word
        QVERIFY(!FuzzyMatch::match("xyz", "getValue"));
        QCOMPARE(FuzzyMatch::match("gsv", "getSelectedValue")->positions, QList<int>({0, 3, 11}));
        QVERIFY(FuzzyMatch::match("", "anything"));
    }

    void fuzzyRanksPrefixesFirst()
    {
        QCOMPARE(ranked("gv", {"getSelectedValue", "setValue", "getValue"}),
                 QStringList({"getValue", "getSelectedValue"}));
        QCOMPARE(ranked("val", {"setValue", "value", "validate"}).first(), QString("value"));
        QCOMPARE(ranked("Val", {"value", "Value"}).first(), QString("Value")); // same case
    }

    void documentWordsAreNearestFirst()
    {
        const QByteArray text = "alpha beta gamma\nbeta zeta alphabet 42abc pri";
        const QStringList words = DocumentWordsProvider::words(text, int(text.size()));
        QVERIFY(!words.contains("pri"));   // the word being typed
        QVERIFY(!words.contains("42abc")); // a number
        QCOMPARE(words.first(), QString("alphabet"));
        QCOMPARE(words.count("beta"), 1);
    }

    void serviceKeepsTheRicherItem()
    {
        QList<CompletionItem> items = {{"run", {}, {}, CompletionItem::Kind::Text, 0}};
        CompletionService::merge(items, {{"run", {}, "Task", CompletionItem::Kind::Method, 4},
                                         {"stop", {}, {}, CompletionItem::Kind::Method, 4}});
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].kind, CompletionItem::Kind::Method);
        QCOMPARE(items[0].detail, QString("Task"));
    }

    void snippetsBecomePlainText()
    {
        ExpandedSnippet e = Snippet::expand("printValue(${1:int value})");
        QCOMPARE(e.text, QString("printValue(int value)"));
        QCOMPARE(e.text.mid(e.selectionStart, e.selectionLength), QString("int value"));
        e = Snippet::expand("clear()");
        QCOMPARE(e.selectionStart, 7); // after the parentheses
        e = Snippet::expand("add($1)$0");
        QCOMPARE(e.text, QString("add()"));
        QCOMPARE(e.selectionStart, 4); // inside
        QCOMPARE(e.selectionLength, 0);
        e = Snippet::expand("for (${1:int} ${2:i} = 0; \\$2 < ${3|n,size|}; ++i) {$0}");
        QCOMPARE(e.text, QString("for (int i = 0; $2 < n; ++i) {}")); // "\\$" is a dollar
        QCOMPARE(e.text.mid(e.selectionStart, e.selectionLength), QString("int"));
        QCOMPARE(Snippet::expand("${TM_SELECTED_TEXT:x} \\$5 cost $").text, QString("x $5 cost $"));
    }

    void acceptedSnippetSelectsTheArgumentAndAddsTheImport()
    {
        const auto late = std::make_shared<LateProvider>();
        const Fixture f("class A {\n    void f() {\n        \n    }\n}\n", {2, 8}, late);
        QTest::keyClicks(f.editor, "Arr");
        QTRY_VERIFY(late->pending);
        CompletionItem list{"ArrayList", "ArrayList<${1:E}>()", "java.util", CompletionItem::Kind::Class, 10};
        list.isSnippet = true;
        list.additionalEdits = {{0, 0, 0, 0, "import java.util.ArrayList;\n\n"}};
        late->pending({list}, false);
        QTRY_VERIFY(f.controller->isActive());
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(0), QString("import java.util.ArrayList;\n"));
        QCOMPARE(f.editor->text(4), QString("        ArrayList<E>()\n"));
        QCOMPARE(f.editor->selectedText(), QString("E")); // typing replaces it
        f.editor->undo();                                 // one step for both
        QCOMPARE(f.editor->text(2), QString("        Arr\n"));
        QVERIFY(!f.editor->text().contains("import"));
    }

    void editsResolvedOnceChosen()
    {
        const auto late = std::make_shared<LateProvider>();
        const Fixture f("class A {\n    void f() {\n        \n    }\n}\n", {2, 8}, late);
        QTest::keyClicks(f.editor, "Lis");
        QTRY_VERIFY(late->pending);
        CompletionItem::EditsCallback resolved;
        CompletionItem list{"List", {}, "java.util", CompletionItem::Kind::Interface, 10};
        list.resolveEdits = [&resolved](const CompletionItem::EditsCallback &done) { resolved = done; };
        late->pending({list}, false);
        QTRY_VERIFY(f.controller->isActive());
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(2), QString("        List\n"));
        QVERIFY(resolved); // asked once chosen
        resolved({{0, 0, 0, 0, "import java.util.List;\n"}});
        QCOMPARE(f.editor->text(0), QString("import java.util.List;\n"));
        QCOMPARE(f.editor->text(3), QString("        List\n"));
    }

    void typingOpensAndEnterInserts()
    {
        const Fixture f("int printValue(int value);\nint main() {\n    \n}\n", {2, 4});
        QTest::keyClicks(f.editor, "pri");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels().first(), QString("printValue"));
        QTest::keyClick(f.editor, Qt::Key_Return);
        QVERIFY(!f.controller->isActive());
        QCOMPARE(f.editor->text(2), QString("    printValue\n"));
        // One undo step brings back what was typed.
        f.editor->undo();
        QCOMPARE(f.editor->text(2), QString("    pri\n"));
    }

    void popupFollowsTheTyping()
    {
        const Fixture f("int printValue;\nint process;\nint main() {\n    \n}\n", {3, 4});
        QTest::keyClicks(f.editor, "p");
        QTRY_VERIFY(f.controller->isActive());
        QVERIFY(f.controller->popup()->labels().contains("process"));
        QTest::keyClicks(f.editor, "rv");
        QCOMPARE(f.controller->popup()->labels().first(), QString("printValue"));
        QVERIFY(!f.controller->popup()->labels().contains("process"));
        QTest::keyClick(f.editor, Qt::Key_Down);
        QTest::keyClick(f.editor, Qt::Key_Escape);
        QVERIFY(!f.controller->isActive());
        QCOMPARE(f.editor->text(3), QString("    prv\n"));
        QTest::keyClick(f.editor, Qt::Key_Space);
        QVERIFY(!f.controller->isActive());
    }

    void editorToolTipsWaitWhileTheListIsOpen()
    {
        Fixture f("int printValue;\nint main() {\n    \n}\n", {2, 4});
        f.editor->setHoverHelp([](int) { return QStringLiteral("Help"); });
        f.window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&f.window));
        QTest::keyClicks(f.editor, "pri");
        QTRY_VERIFY(f.controller->isActive());

        emit f.editor->SCN_DWELLSTART(0, 10, 10);

        QVERIFY(f.editor->areToolTipsSuspended());
        QVERIFY(!QToolTip::isVisible());
    }

    void editorToolTipsComeBackOnceTheListCloses()
    {
        Fixture f("int printValue;\nint main() {\n    \n}\n", {2, 4});
        f.editor->setHoverHelp([](int) { return QStringLiteral("Help"); });
        f.window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&f.window));
        QTest::keyClicks(f.editor, "pri");
        QTRY_VERIFY(f.controller->isActive());
        QTest::keyClick(f.editor, Qt::Key_Escape);

        emit f.editor->SCN_DWELLSTART(0, 10, 10);

        QVERIFY(!f.editor->areToolTipsSuspended());
        QTRY_VERIFY(QToolTip::isVisible() && QToolTip::text().contains("Help"));
        QToolTip::hideText();
    }

    void openingTheListHidesTheToolTipShown()
    {
        const Fixture f("int printValue;\nint main() {\n    \n}\n", {2, 4});
        QToolTip::showText(f.editor->viewport()->mapToGlobal(QPoint(10, 10)), QStringLiteral("Server message"),
                           f.editor->viewport());
        QTRY_VERIFY(QToolTip::isVisible());

        QTest::keyClicks(f.editor, "pri");
        QTRY_VERIFY(f.controller->isActive());

        QTRY_VERIFY(!QToolTip::isVisible());
    }

    void modesDecideWhenThePopupOpens()
    {
        {
            const Fixture f("int printValue;\nint main() {\n    \n}\n", {2, 4});
            f.editor->setOptions({.autoCompletion = AutoCompletion::Off});
            QTest::keyClicks(f.editor, "pri");
            QTest::qWait(150);
            QVERIFY(!f.controller->isActive());
            f.controller->complete(); // Ctrl+Space still opens it
            QTRY_VERIFY(f.controller->isActive());
        }
        {
            const Fixture f("int printValue;\nint main() {\n    \n}\n", {2, 4});
            f.editor->setOptions({.autoCompletion = AutoCompletion::Calm});
            QTest::keyClicks(f.editor, "pr");
            QTest::qWait(500);
            QVERIFY(!f.controller->isActive()); // two letters: too early
            QTest::keyClicks(f.editor, "i");
            QTest::qWait(150);
            QVERIFY(!f.controller->isActive()); // not before a pause
            QTRY_VERIFY(f.controller->isActive());
        }
    }

    void enterCanBeLeftToNewLines()
    {
        const Fixture f("int printValue;\nint main() {\n    \n}\n", {2, 4});
        f.editor->setOptions({.completionAcceptsEnter = false});
        QTest::keyClicks(f.editor, "pri");
        QTRY_VERIFY(f.controller->isActive());
        QVERIFY(f.controller->popup()->keyHint().startsWith("⇥ insert"));
        QTest::keyClick(f.editor, Qt::Key_Return);
        QVERIFY(!f.controller->isActive());
        QCOMPARE(f.editor->text(2), QString("    pri\n"));
        QTest::keyClicks(f.editor, "pri");
        QTRY_VERIFY(f.controller->isActive());
        QTest::keyClick(f.editor, Qt::Key_Tab);
        QCOMPARE(f.editor->text(3), QString("    printValue\n"));
    }

    void ctrlRightInsertsTheNextWord()
    {
        const Fixture f("int getSelectedValue;\nint main() {\n    \n}\n", {2, 4});
        QTest::keyClicks(f.editor, "get");
        QTRY_VERIFY(f.controller->isActive());
        QTest::keyClick(f.editor, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(f.editor->text(2), QString("    getSelected\n"));
        QVERIFY(f.controller->isActive()); // the list follows
        QTest::keyClick(f.editor, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(f.editor->text(2), QString("    getSelectedValue\n"));
        QVERIFY(!f.controller->isActive()); // the whole name: inserted
    }

    void previewShowsTheRestAtTheEndOfALine()
    {
        const Fixture f("int printValue;\nint main() {\n    \n    x = 1;\n}\n", {2, 4});
        QTest::keyClicks(f.editor, "pri");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->previewText(), QString("ntValue"));
        QTest::keyClick(f.editor, Qt::Key_Escape);
        QVERIFY(f.controller->previewText().isEmpty());
        // Code after the cursor: no room for it.
        f.editor->setCursorPosition(3, 4);
        QTest::keyClicks(f.editor, "pri");
        QTRY_VERIFY(f.controller->isActive());
        QVERIFY(f.controller->previewText().isEmpty());
    }

    void documentationBesideTheList()
    {
        const auto late = std::make_shared<LateProvider>();
        const Fixture f("int main() {\n    \n}\n", {1, 4}, late);
        f.controller->complete();
        QTRY_VERIFY(late->pending);
        int resolved = 0;
        CompletionItem documented{"printValue", {}, "void printValue(int)", CompletionItem::Kind::Function, 10};
        documented.documentation = "Prints the value.";
        CompletionItem lazy{"printOther", {}, {}, CompletionItem::Kind::Method, 9};
        lazy.resolveDocumentation = [&resolved](const CompletionItem::TextCallback &done) {
            ++resolved;
            done("Asked once.");
        };
        late->pending({documented, lazy}, false);
        QTRY_VERIFY(f.controller->isActive());
        CompletionPopup *popup = f.controller->popup();
        popup->setDocumentationVisible(true);
        QVERIFY(popup->documentationText().contains("void printValue(int)"));
        QVERIFY(popup->documentationText().contains("function"));
        QVERIFY(popup->documentationText().contains("Prints the value."));
        QTest::keyClick(f.editor, Qt::Key_Down);
        QTRY_VERIFY(popup->documentationText().contains("Asked once."));
        QTest::keyClick(f.editor, Qt::Key_Up);
        QTest::keyClick(f.editor, Qt::Key_Down);
        QCOMPARE(resolved, 1);
        // Ctrl+Q hides it, and shows it again.
        QTest::keyClick(f.editor, Qt::Key_Q, Qt::ControlModifier);
        QVERIFY(popup->documentationText().isEmpty());
        QTest::keyClick(f.editor, Qt::Key_Q, Qt::ControlModifier);
        QVERIFY(!popup->documentationText().isEmpty());
    }

    void screenReadersHearTheList()
    {
        static QStringList announced;
        announced.clear();
        QAccessible::installUpdateHandler([](QAccessibleEvent *event) {
            if (event->type() == QAccessible::Announcement)
                announced.append(static_cast<QAccessibleAnnouncementEvent *>(event)->message());
        });
        QAccessible::setActive(true);
        const Fixture f("int printValue;\nint process;\nint main() {\n    \n}\n", {3, 4});
        QTest::keyClicks(f.editor, "p");
        QTRY_VERIFY(f.controller->isActive());
        QTRY_VERIFY(!announced.isEmpty());
        QVERIFY2(announced.first().contains("suggestions") && announced.first().contains("of"),
                 qPrintable(announced.first()));
        QTest::keyClick(f.editor, Qt::Key_Down);
        QVERIFY2(announced.last().contains(", word,") || announced.last().contains(", keyword,"),
                 qPrintable(announced.last()));
        const QModelIndex row = f.controller->popup()->findChild<QListView *>()->model()->index(0, 0);
        QVERIFY(row.data(Qt::AccessibleTextRole).toString().contains(", "));
        QAccessible::installUpdateHandler(nullptr);
        QAccessible::setActive(false);
    }

    void historyRanksPastChoices()
    {
        const QDateTime now = QDateTime::currentDateTime();
        CompletionHistory history;
        QCOMPARE(history.boost("cpp", "value"), 0);
        history.recordAccepted("cpp", "value", now);
        const int once = history.boost("cpp", "value", now);
        history.recordAccepted("cpp", "value", now);
        history.recordAccepted("cpp", "value", now);
        QVERIFY(history.boost("cpp", "value", now) > once);
        QVERIFY(history.boost("cpp", "value", now.addDays(60)) < history.boost("cpp", "value", now)); // it fades
        QCOMPARE(history.boost("java", "value", now), 0);                                             // per language
        history.recordRejected("cpp", "wrong");
        history.recordRejected("cpp", "wrong");
        QVERIFY(history.boost("cpp", "wrong", now) < 0);

        // Kept in its file.
        const QTemporaryDir dir;
        {
            CompletionHistory saved;
            saved.setFile(dir.filePath("history.json"));
            saved.recordAccepted("cpp", "kept", now);
        }
        CompletionHistory loaded;
        loaded.setFile(dir.filePath("history.json"));
        QVERIFY(loaded.boost("cpp", "kept", now) > 0);
        QVERIFY(CompletionHistory::fileFor("/a").endsWith(".json"));
        QVERIFY(CompletionHistory::fileFor("/a") != CompletionHistory::fileFor("/b"));
    }

    void chosenCandidatesRankHigherAndUndoneOnesLower()
    {
        Fixture f("int printAlpha;\nint printBeta;\nint main() {\n    \n}\n", {3, 4});
        QTest::keyClicks(f.editor, "print");
        QTRY_VERIFY(f.controller->isActive());
        const QString first = f.controller->popup()->labels().first();
        const QString other = first == "printAlpha" ? "printBeta" : "printAlpha";
        // Choosing the other one a few times brings it first.
        for (int i = 0; i < 3; ++i)
            f.service.history()->recordAccepted("cpp", other);
        QTest::keyClick(f.editor, Qt::Key_Escape);
        QTest::keyClick(f.editor, Qt::Key_Backspace);
        QTest::keyClicks(f.editor, "t");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels().first(), other);
        // Inserted, then undone at once: a rejection.
        const int before = f.service.history()->boost("cpp", other);
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(3), QString("    %1\n").arg(other));
        f.editor->undo();
        QVERIFY(f.service.history()->boost("cpp", other) < before);
    }

    // Benchmarks of what runs at each keystroke while the list is open.
    void filtersManyCandidates()
    {
        QList<CompletionItem> items;
        for (int i = 0; i < 20000; ++i) {
            CompletionItem item;
            item.label = QStringLiteral("get%1SelectedValue%2").arg(i % 7 == 0 ? "Item" : "Other").arg(i);
            items.append(item);
        }
        QWidget parent;
        CompletionPopup popup(&parent);
        QBENCHMARK
        {
            popup.setItems(items, QStringLiteral("gsv"));
        }
        QCOMPARE(popup.count(), 200);
    }

    void popupGrowsWithTheCodeFont()
    {
        QWidget parent;
        parent.resize(1600, 1200);
        CompletionItem item;
        item.label = QStringLiteral("printValue");
        CompletionPopup popup(&parent);
        popup.setItems({item}, QString());
        Theme theme = Theme::dark();
        popup.applyTheme(theme);
        popup.showAt({0, 0}, 0);
        const QSize small = popup.size();

        theme.font.setPointSizeF(theme.font.pointSizeF() * 2);
        popup.applyTheme(theme);
        popup.showAt({0, 0}, 0);

        QVERIFY(popup.width() > small.width());
        QVERIFY(popup.height() > small.height());
    }

    void popupFollowsTheEditorZoom()
    {
        const Fixture f("int printValue;\nint x = pri;\n", {1, 11});
        f.controller->complete();
        QTRY_VERIFY(f.controller->isActive());
        const int unzoomedHeight = f.controller->popup()->height();
        QTest::keyClick(f.editor, Qt::Key_Escape);
        QTRY_VERIFY(!f.controller->isActive());

        f.editor->SendScintilla(QsciScintillaBase::SCI_SETZOOM, 8);
        f.controller->complete();
        QTRY_VERIFY(f.controller->isActive());

        QVERIFY(f.controller->popup()->height() > unzoomedHeight);
    }

    void collectsDocumentWords()
    {
        QByteArray text;
        for (int i = 0; i < 20000; ++i)
            text += "    const int value" + QByteArray::number(i % 500) + " = compute(first, second);\n";
        QBENCHMARK
        {
            DocumentWordsProvider::words(text, int(text.size() / 2));
        }
    }

    void tabReplacesTheWholeIdentifier()
    {
        const Fixture f("int printValue;\nint x = prinOld;\n", {1, 11});
        f.controller->complete();
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels().first(), QString("printValue"));
        QTest::keyClick(f.editor, Qt::Key_Tab);
        QCOMPARE(f.editor->text(1), QString("int x = printValue;\n"));
    }

    void noPopupInCommentsOrStrings()
    {
        const Fixture f("int printValue;\n// \nconst char *s = \"\";\n", {1, 3});
        QTest::keyClicks(f.editor, "pri");
        QTest::qWait(150);
        QVERIFY(!f.controller->isActive());
        f.editor->setCursorPosition(2, 17);
        QTest::keyClicks(f.editor, "pri");
        QTest::qWait(150);
        QVERIFY(!f.controller->isActive());
        // Ctrl+Space still works there.
        f.controller->complete();
        QTRY_VERIFY(f.controller->isActive());
    }

    void lateAnswersJoinTheList()
    {
        const auto late = std::make_shared<LateProvider>();
        const Fixture f("int printValue;\nint main() {\n    \n}\n", {2, 4}, late);
        QTest::keyClicks(f.editor, "pri");
        QTRY_VERIFY(f.controller->isActive());
        QVERIFY(late->pending);
        late->pending({{"printf", {}, "int printf(const char *, ...)", CompletionItem::Kind::Function, 10}}, false);
        QCOMPARE(f.controller->popup()->labels().first(), QString("printf"));
        // A trigger character opens completion with no identifier typed.
        QTest::keyClick(f.editor, Qt::Key_Escape);
        late->pending = nullptr;
        QTest::keyClicks(f.editor, ".");
        QVERIFY(late->pending);
        // After it, words of the file are no members: only the server answers.
        QTest::keyClick(f.editor, Qt::Key_Escape);
        f.controller->complete();
        QTRY_VERIFY(late->pending);
        late->pending({{"println", {}, {}, CompletionItem::Kind::Method, 10}}, false);
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels(), QStringList{"println"});
    }

    void snippetStopsAndMirrors()
    {
        const ExpandedSnippet e = Snippet::expand("for (int ${1:i} = 0; $1 < ${2:n}; ++$1) {$0}");
        QCOMPARE(e.text, QString("for (int i = 0; i < n; ++i) {}"));
        QCOMPARE(e.stops.size(), 3);
        QCOMPARE(e.stops[0].index, 1);
        QCOMPARE(e.stops[0].ranges.size(), 3); // the placeholder and its two copies
        QCOMPARE(e.text.mid(e.stops[0].ranges[2].start, e.stops[0].ranges[2].length), QString("i"));
        QCOMPARE(e.stops[1].index, 2);
        QCOMPARE(e.stops[2].index, 0); // last
        QCOMPARE(e.stops[2].ranges[0].start, int(e.text.size()) - 1);
    }

    void tabFillsThePlaceholdersInTurn()
    {
        const auto late = std::make_shared<LateProvider>();
        const Fixture f("int main() {\n    \n}\n", {1, 4}, late);
        QTest::keyClicks(f.editor, "lo");
        QTRY_VERIFY(late->pending);
        CompletionItem loop{
            "loop", "for (int ${1:i} = 0; $1 < ${2:n}; ++$1) {$0}", {}, CompletionItem::Kind::Snippet, 10};
        loop.isSnippet = true;
        late->pending({loop}, false);
        QTRY_VERIFY(f.controller->isActive());
        QTest::keyClick(f.editor, Qt::Key_Return);
        const SnippetSession *snippet = f.controller->snippet();
        QVERIFY(snippet->isActive());
        QCOMPARE(f.editor->selectedText(), QString("i"));
        // Typed in the first, copied to the others.
        QTest::keyClicks(f.editor, "k");
        QTRY_COMPARE(f.editor->text(1), QString("    for (int k = 0; k < n; ++k) {}\n"));
        QTest::keyClick(f.editor, Qt::Key_Tab);
        QCOMPARE(snippet->currentIndex(), 2);
        QCOMPARE(f.editor->selectedText(), QString("n"));
        QTest::keyClicks(f.editor, "10");
        QTest::keyClick(f.editor, Qt::Key_Backtab);
        QCOMPARE(f.editor->selectedText(), QString("k"));
        QTest::keyClick(f.editor, Qt::Key_Tab);
        QTest::keyClick(f.editor, Qt::Key_Tab); // $0: done
        QVERIFY(!snippet->isActive());
        int line = 0;
        int index = 0;
        f.editor->getCursorPosition(&line, &index);
        QCOMPARE(index, 34); // between the braces
        QCOMPARE(f.editor->text(1), QString("    for (int k = 0; k < 10; ++k) {}\n"));
        // Tab indents again.
        QTest::keyClick(f.editor, Qt::Key_Tab);
        QVERIFY(f.editor->text(1).size() > 37);
    }

    void snippetEndsWhenTheCursorLeaves()
    {
        const auto late = std::make_shared<LateProvider>();
        const Fixture f("int main() {\n    \n}\n", {1, 4}, late);
        QTest::keyClicks(f.editor, "pri");
        QTRY_VERIFY(late->pending);
        CompletionItem call{"print", "print(${1:int a}, ${2:int b})", {}, CompletionItem::Kind::Function, 10};
        call.isSnippet = true;
        late->pending({call}, false);
        QTRY_VERIFY(f.controller->isActive());
        QTest::keyClick(f.editor, Qt::Key_Return);
        QVERIFY(f.controller->snippet()->isActive());
        f.editor->setCursorPosition(0, 0);
        QTRY_VERIFY(!f.controller->snippet()->isActive());
    }

    void postfixFindsTheExpression()
    {
        const auto start = [](const QByteArray &text) {
            const int dot = int(text.lastIndexOf('.'));
            const int at = PostfixTemplateProvider::expressionStart(text, dot);
            return at < 0 ? QByteArray("<none>") : text.mid(at, dot - at);
        };
        QCOMPARE(start("    items."), QByteArray("items"));
        QCOMPARE(start("x = this->items[i].name."), QByteArray("this->items[i].name"));
        QCOMPARE(start("f(a, g(b)).get()."), QByteArray("f(a, g(b)).get()"));
        QCOMPARE(start("    (a + b)."), QByteArray("(a + b)"));
        QCOMPARE(start("auto p = std::make_unique<Foo>(1)."), QByteArray("std::make_unique<Foo>(1)"));
        QCOMPARE(start("return new Foo(\"a.b\")."), QByteArray("new Foo(\"a.b\")"));
        QCOMPARE(start("a?.b?."), QByteArray("a?.b?")); // the provider leaves "?" out
        QCOMPARE(start("if (x > 0 && list.size()."), QByteArray("list.size()"));
        QCOMPARE(start("    ."), QByteArray("<none>"));
        QCOMPARE(start("call(a, b)) ."), QByteArray("<none>"));
    }

    void postfixSuggestsNames()
    {
        QCOMPARE(PostfixTemplateProvider::suggestName("widget.getSelectedItem()"), QString("selectedItem"));
        QCOMPARE(PostfixTemplateProvider::suggestName("items.size()"), QString("size"));
        QCOMPARE(PostfixTemplateProvider::suggestName("get_name(x)"), QString("name"));
        QCOMPARE(PostfixTemplateProvider::suggestName("new Widget(parent)"), QString("widget"));
        QCOMPARE(PostfixTemplateProvider::suggestName("count"), QString("value")); // its own name
        QCOMPARE(PostfixTemplateProvider::suggestName("42"), QString("value"));
    }

    void postfixWrapsTheExpression()
    {
        Fixture f("int main() {\n    items\n}\n", {1, 9});
        f.service.addProvider(std::make_shared<PostfixTemplateProvider>());
        QTest::keyClicks(f.editor, ".if");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels().first(), QString("if"));
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(), QString("int main() {\n    if (items) {\n        \n    }\n}\n"));
        int line = 0;
        int index = 0;
        f.editor->getCursorPosition(&line, &index);
        QCOMPARE(line, 2);
        QCOMPARE(index, 8);
        f.editor->undo(); // one step
        QCOMPARE(f.editor->text(1), QString("    items.if\n"));
    }

    void postfixVarNamesTheValue()
    {
        Fixture f("int main() {\n    widget.getSelectedItem()\n}\n", {1, 28});
        f.service.addProvider(std::make_shared<PostfixTemplateProvider>());
        QTest::keyClicks(f.editor, ".var");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels().first(), QString("var"));
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(1), QString("    auto selectedItem = widget.getSelectedItem();\n"));
        QCOMPARE(f.editor->selectedText(), QString("selectedItem"));
        QTest::keyClick(f.editor, Qt::Key_Tab);
        QVERIFY(!f.controller->snippet()->isActive());
    }

    void postfixStatementsOnlyStartStatements()
    {
        Fixture f("int main() {\n    int n = items\n}\n", {1, 17});
        f.service.addProvider(std::make_shared<PostfixTemplateProvider>());
        QTest::keyClicks(f.editor, "."); // opens the list
        QTRY_VERIFY(f.controller->isActive());
        const QStringList labels = f.controller->popup()->labels();
        QVERIFY(!labels.contains("if"));
        QVERIFY(!labels.contains("return"));
        QVERIFY(labels.contains("static_cast"));
        QTest::keyClicks(f.editor, "sc");
        QCOMPARE(f.controller->popup()->labels().first(), QString("static_cast"));
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(1), QString("    int n = static_cast<type>(items)\n"));
        QCOMPARE(f.editor->selectedText(), QString("type"));
    }

    void liveTemplatesExpand()
    {
        Fixture f("int main() {\n    \n}\n", {1, 4});
        f.service.addProvider(std::make_shared<LiveTemplateProvider>());
        QTest::keyClicks(f.editor, "iter");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels().first(), QString("iter"));
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(1), QString("    for (auto &item : container) {\n"));
        QCOMPARE(f.editor->text(2), QString("        \n"));
        QCOMPARE(f.editor->text(3), QString("    }\n"));
        QCOMPARE(f.editor->selectedText(), QString("container"));
        QTest::keyClicks(f.editor, "names");
        QTRY_VERIFY(f.controller->isActive()); // "namespace"
        QTest::keyClick(f.editor, Qt::Key_Escape);
        QVERIFY(f.controller->snippet()->isActive()); // the first Escape closed the list
        QTest::keyClick(f.editor, Qt::Key_Tab);
        QCOMPARE(f.editor->selectedText(), QString("item"));
        QCOMPARE(f.editor->text(1), QString("    for (auto &item : names) {\n"));
        // Keywords come first; the template of the same name follows.
        f.editor->setCursorPosition(4, 0);
        QTest::keyClick(f.editor, Qt::Key_Escape);
        QTest::keyClicks(f.editor, "for");
        QTRY_VERIFY(f.controller->isActive());
        const QStringList labels = f.controller->popup()->labels();
        QCOMPARE(labels.count("for"), 2);
    }

    void secondCompletionShowsMore()
    {
        Fixture g("int widget;\nint getValue;\nint main() {\n    \n}\n", {3, 4});
        // Another open file of the language.
        QFile moreFile(g.dir.filePath("b.cpp"));
        QVERIFY(moreFile.open(QIODevice::WriteOnly));
        moreFile.write("int getterElsewhere;\n");
        moreFile.close();
        auto *more = new EditorWidget(Theme::dark(), &g.window);
        more->load(g.dir.filePath("b.cpp"), nullptr);
        g.editor->setFocus();
        QTest::keyClicks(g.editor, "get");
        QTRY_VERIFY(g.controller->isActive());
        QVERIFY(!g.controller->popup()->labels().contains("widget"));
        QVERIFY(!g.controller->popup()->labels().contains("getterElsewhere"));
        QVERIFY(g.controller->popup()->keyHint().contains("Ctrl+Space more"));
        g.controller->complete();
        QCOMPARE(g.controller->mode(), CompletionContext::Mode::Extended);
        QTRY_VERIFY(g.controller->popup()->labels().contains("widget"));        // matched inside the word
        QVERIFY(g.controller->popup()->labels().contains("getterElsewhere"));   // the other file's
        QCOMPARE(g.controller->popup()->labels().first(), QString("getValue")); // a prefix still first
        QVERIFY(g.controller->popup()->keyHint().startsWith("All suggestions"));
        // A third time: the documentation.
        const bool shown = g.controller->popup()->isDocumentationVisible();
        g.controller->complete();
        QCOMPARE(g.controller->popup()->isDocumentationVisible(), !shown);
        g.controller->popup()->setDocumentationVisible(shown);
    }

    void expectedTypeFromTheText()
    {
        const auto at = [](const QByteArray &text, const QString &language = QStringLiteral("cpp")) {
            return ExpectedType::at(text, int(text.size()), language);
        };
        QCOMPARE(at("void f() {\n    Widget *w = "), QString("Widget *"));
        QCOMPARE(at("void f() {\n    const std::string &name = "), QString("std::string &"));
        QCOMPARE(at("void f() {\n    int count = 0;\n    count = "), QString("int"));
        QCOMPARE(at("void f() {\n    QList<int> items;\n    items = "), QString("QList<int>"));
        QCOMPARE(at("class A { double ratio; void f() { this->ratio = "), QString("double"));
        QCOMPARE(at("int size() const {\n    if (x) {\n        return "), QString("int"));
        QCOMPARE(at("void f() {\n    if ("), QString("bool"));
        QCOMPARE(at("void f() {\n    ok = a && "), QString("bool"));
        QCOMPARE(at("void f() {\n    auto x = "), QString());
        QCOMPARE(at("void f() {\n    call("), QString()); // the server's signature help knows
        QCOMPARE(at("String name() {\n    return ", "java"), QString("String"));
        QCOMPARE(at("let total: number = ", "typescript"), QString("number"));
        QCOMPARE(at("let total = ", "javascript"), QString());
        QCOMPARE(at("for (int i = 0; i < n; ++i) {\n    i = "), QString("int"));
    }

    void smartCompletionKeepsTheExpectedType()
    {
        using Kind = CompletionItem::Kind;
        CompletionItem count{"count", {}, {}, Kind::Variable};
        count.type = "int";
        CompletionItem name{"name", {}, {}, Kind::Variable};
        name.type = "const QString &";
        CompletionItem length{"length", {}, {}, Kind::Method};
        length.type = ExpectedType::ofDetail("String.length() : long");
        const CompletionItem widget{"Widget", {}, {}, Kind::Class};
        QVERIFY(ExpectedType::accepts("int", count));
        QVERIFY(ExpectedType::accepts("size_t", count)); // numbers go together
        QVERIFY(!ExpectedType::accepts("int", name));
        QVERIFY(ExpectedType::accepts("QString", name));
        QVERIFY(ExpectedType::accepts("int", length));
        QVERIFY(ExpectedType::accepts("Widget *", widget, true)); // "new |"
        QVERIFY(!ExpectedType::accepts("Widget *", count, true));
        QVERIFY(!ExpectedType::accepts("int", widget));
        QCOMPARE(ExpectedType::ofParameter("const QString &text", "cpp"), QString("const QString &"));
        QCOMPARE(ExpectedType::ofParameter("size: number = 3", "typescript"), QString("number"));

        // Only the smart providers, told the type.
        const auto smart = std::make_shared<RecordingProvider>(QList<CompletionItem>{count, name}, true);
        smart->items.removeIf([](const CompletionItem &item) { return item.label != "count"; });
        Fixture f("int main() {\n    int total = \n}\n", {1, 16}, smart);
        f.service.addProvider(std::make_shared<PostfixTemplateProvider>());
        f.controller->completeSmart();
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(smart->last.mode, CompletionContext::Mode::Smart);
        QCOMPARE(smart->last.expectedType, QString("int"));
        QCOMPARE(f.controller->popup()->labels(), QStringList({"count"})); // no words, no keywords
        QVERIFY(f.controller->popup()->keyHint().startsWith("Expected int"));
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(1), QString("    int total = count\n"));
        // A boolean: the keywords "true" and "false".
        const Fixture g("int main() {\n    bool ok = \n}\n", {1, 14});
        g.controller->completeSmart();
        QTRY_VERIFY(g.controller->isActive());
        QCOMPARE(g.controller->popup()->labels(), QStringList({"true", "false"}));
    }

    void fullLineSuggestsTheRestOfTheLine()
    {
        QCOMPARE(FullLineModel::lineOf("ue);\nmore", QString()), QString("ue);"));
        QCOMPARE(FullLineModel::lineOf("value);  ", ");"), QString("value"));
        QCOMPARE(FullLineModel::lineOf("   \n", QString()), QString());

        Fixture f("int main() {\n    \n}\n", {1, 4});
        f.editor->setOptions({.autoCompletion = AutoCompletion::Off});
        FakeFullLineModel model;
        model.answer = "Value(total);";
        f.service.setFullLineModel(&model);
        QTest::keyClicks(f.editor, "print");
        QTRY_COMPARE(f.controller->fullLineText(), QString("Value(total);"));
        QCOMPARE(model.lastBefore, QString("int main() {\n    print"));
        QCOMPARE(model.lastAfter, QString("\n}\n"));
        QCOMPARE(f.controller->previewText(), QString("Value(total);"));
        // Typing what it shows keeps the rest.
        QTest::keyClicks(f.editor, "Va");
        QCOMPARE(f.controller->fullLineText(), QString("lue(total);"));
        // Ctrl+Right takes a word, Tab the rest.
        QTest::keyClick(f.editor, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(f.editor->text(1), QString("    printValue\n"));
        QCOMPARE(f.controller->fullLineText(), QString("(total);"));
        QTest::keyClick(f.editor, Qt::Key_Tab);
        QCOMPARE(f.editor->text(1), QString("    printValue(total);\n"));
        QVERIFY(f.controller->fullLineText().isEmpty());
        // Something else typed, or Escape: gone.
        QTest::keyClick(f.editor, Qt::Key_Return);
        QTest::keyClicks(f.editor, "x");
        QTRY_VERIFY(!f.controller->fullLineText().isEmpty());
        QTest::keyClick(f.editor, Qt::Key_Escape);
        QVERIFY(f.controller->fullLineText().isEmpty());
        QVERIFY(f.controller->previewText().isEmpty());
        QTest::keyClicks(f.editor, "y");
        QTRY_VERIFY(!f.controller->fullLineText().isEmpty());
        QTest::keyClicks(f.editor, "z");
        QVERIFY(f.controller->fullLineText().isEmpty() || f.controller->fullLineText() == model.answer);
        // Not where code follows the cursor.
        const int requests = model.requests;
        f.editor->setCursorPosition(0, 3);
        QTest::keyClicks(f.editor, "q");
        QTest::qWait(400);
        QCOMPARE(model.requests, requests);
    }

    void fullLineAsksTheLocalServer()
    {
        // A server answering each request with `answer`, keeping what it got.
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray received;
        QByteArray answer;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                received += socket->readAll();
                const int headerEnd = int(received.indexOf("\r\n\r\n"));
                const int length = received.mid(received.toLower().indexOf("content-length:") + 15)
                                       .split('\r')
                                       .first()
                                       .trimmed()
                                       .toInt();
                if (headerEnd < 0 || received.size() < headerEnd + 4 + length)
                    return;
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                              QByteArray::number(answer.size()) + "\r\nConnection: close\r\n\r\n" + answer);
                socket->disconnectFromHost();
            });
        });
        const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
        LocalFullLineModel model;
        QString suggestion = "<none>";
        model.configure(LocalFullLineModel::Server::Ollama, endpoint, "coder");
        answer = R"({"response": "Value(x);\nreturn 0;"})";
        model.suggest("print", "\n}", "cpp", [&](const QString &text) { suggestion = text; });
        QTRY_COMPARE(suggestion, QString("Value(x);"));
        QVERIFY(received.startsWith("POST /api/generate "));
        QJsonObject body = QJsonDocument::fromJson(received.mid(received.indexOf("\r\n\r\n") + 4)).object();
        QCOMPARE(body.value("model").toString(), QString("coder"));
        QCOMPARE(body.value("prompt").toString(), QString("print"));
        QCOMPARE(body.value("suffix").toString(), QString("\n}"));

        received.clear();
        suggestion = "<none>";
        model.configure(LocalFullLineModel::Server::LlamaCpp, endpoint, {});
        answer = R"({"content": " + 1;"})";
        model.suggest("x = y", "", "cpp", [&](const QString &text) { suggestion = text; });
        QTRY_COMPARE(suggestion, QString(" + 1;"));
        QVERIFY(received.startsWith("POST /infill "));
        body = QJsonDocument::fromJson(received.mid(received.indexOf("\r\n\r\n") + 4)).object();
        QCOMPARE(body.value("input_prefix").toString(), QString("x = y"));

        // No server: an empty suggestion, the failure said once.
        server.close();
        const QSignalSpy failures(&model, &LocalFullLineModel::failed);
        suggestion = "<none>";
        model.suggest("a", "", "cpp", [&](const QString &text) { suggestion = text; });
        QTRY_COMPARE(suggestion, QString());
        model.suggest("a", "", "cpp", [](const QString &) { });
        QTRY_COMPARE(failures.count(), 1);
    }

    void docCommentOfTheFunctionBelow()
    {
        QCOMPARE(DocComments::commentStart("/** abc @", 9, "cpp"), 0);
        QCOMPARE(DocComments::commentStart("/* abc @", 8, "cpp"), -1); // not a documentation comment
        QCOMPARE(DocComments::commentStart("/** a */ x", 10, "cpp"), -1);
        QCOMPARE(DocComments::commentStart("    /// see @", 13, "cpp"), 4);

        const Fixture f("\ntemplate <typename T>\nint add(int a, const T &b = {});\n", {0, 0});
        QTest::keyClicks(f.editor, "/**");
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(), QString("/**\n * \n *\n * @tparam T \n * @param a \n * @param b \n * @return \n */\n"
                                           "template <typename T>\nint add(int a, const T &b = {});\n"));
        QCOMPARE(cursorLine(f.editor), 1); // the summary first
        QVERIFY(f.controller->snippet()->isActive());
        QTest::keyClicks(f.editor, "Adds them.");
        QTest::keyClick(f.editor, Qt::Key_Tab);
        QCOMPARE(cursorLine(f.editor), 3); // the template parameter's text
        // Java: "@throws".
        const Fixture java("\npublic int read(byte[] buffer) throws IOException {\n}\n", {0, 0}, {}, "A.java");
        QTest::keyClicks(java.editor, "/**");
        QTest::keyClick(java.editor, Qt::Key_Return);
        QVERIFY(java.editor->text().contains(" * @param buffer \n * @return \n * @throws IOException \n */\n"));
        // Nothing to document: the summary only.
        const Fixture empty("\n", {0, 0});
        QTest::keyClicks(empty.editor, "/**");
        QTest::keyClick(empty.editor, Qt::Key_Return);
        QCOMPARE(empty.editor->text(), QString("/**\n * \n */\n"));
    }

    void docCommentTagsAndParameters()
    {
        Fixture f("/**\n * \n */\nint add(int first, int second);\n", {1, 3});
        f.service.addProvider(std::make_shared<DocCommentProvider>());
        QTest::keyClicks(f.editor, "@");
        QTRY_VERIFY(f.controller->isActive());
        QVERIFY(f.controller->popup()->labels().contains("param"));
        QVERIFY(f.controller->popup()->labels().contains("brief"));
        QVERIFY(!f.controller->popup()->labels().contains("int")); // no keywords in comments
        QTest::keyClicks(f.editor, "par");
        QCOMPARE(f.controller->popup()->labels().first(), QString("param"));
        QTest::keyClick(f.editor, Qt::Key_Return);
        QTest::keyClicks(f.editor, " ");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels(), QStringList({"first", "second"}));
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(1), QString(" * @param first\n"));
        // The next "@param": only the one left.
        QTest::keyClick(f.editor, Qt::Key_End);
        QTest::keyClicks(f.editor, " x @param ");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels(), QStringList({"second"}));
        QTest::keyClick(f.editor, Qt::Key_Escape);
        // "\" in C++ too.
        QTest::keyClicks(f.editor, "\\ret");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels().first(), QString("return"));
    }

    void codeScanReadsDeclarations()
    {
        CodeScan::Function f =
            CodeScan::parseFunction("virtual QSize sizeHint(int a, const QString &b = {}) const override");
        QCOMPARE(f.name, QString("sizeHint"));
        QCOMPARE(f.returnType, QString("QSize"));
        QVERIFY(f.specifiers.contains("virtual"));
        QCOMPARE(f.qualifiers(), QString("const"));
        QCOMPARE(f.parameterList().size(), 2);
        QCOMPARE(f.parameterList()[1].name, QString("b"));
        QCOMPARE(f.parameterList()[1].text, QString("const QString &b"));
        QCOMPARE(f.parameterList()[1].defaultValue, QString("{}"));
        f = CodeScan::parseFunction("int Foo::Bar::size() const");
        QCOMPARE(f.scope, QString("Foo::Bar"));
        QCOMPARE(f.name, QString("size"));
        QVERIFY(CodeScan::parseFunction("virtual void paint() = 0").pure);
        QVERIFY(CodeScan::parseFunction("if (x > 0)").name.isEmpty());
        QVERIFY(CodeScan::parseFunction("int x = f(y)").name.isEmpty());
        QVERIFY(CodeScan::parseFunction("return f(y)").name.isEmpty());
        QVERIFY(CodeScan::parseFunction("a.f(b)").name.isEmpty());
        f = CodeScan::parseFunction("@Override\npublic java.util.List<String> names(int max) throws IOException");
        QCOMPARE(f.name, QString("names"));
        QCOMPARE(f.returnType, QString("java.util.List<String>"));
        QVERIFY(f.specifiers.contains("@Override"));
        QCOMPARE(CodeScan::parseFunction("Widget(QWidget *parent = nullptr)").returnType, QString());
        QCOMPARE(CodeScan::parseParameter("void (*callback)(int)").name, QString("callback"));
        QCOMPARE(CodeScan::parseParameter("int").name, QString());
        QCOMPARE(CodeScan::parseParameter("char *argv[]").name, QString("argv"));
        QCOMPARE(CodeScan::parseParameter("...rest", false).name, QString("rest"));

        const QByteArray text =
            "// class Fake {\nclass Q_DECL_EXPORT Widget final : public QObject, private Base<int> {\n"
            "    Q_OBJECT\npublic:\n    void a();\nsignals:\n    void changed();\n"
            "private:\n    int b() const { return 1; }\n    struct Inner { int x; };\n};\n";
        const QByteArray blanked = CodeScan::blank(text);
        QCOMPARE(blanked.size(), text.size());
        const int body = CodeScan::classBody(blanked, "Widget");
        QCOMPARE(text[body], '{');
        const CodeScan::ClassHeader header = CodeScan::classAt(blanked, body);
        QCOMPARE(header.name, QString("Widget"));
        QCOMPARE(header.bases, QStringList({"QObject", "Base"}));
        const QList<CodeScan::Declaration> declarations = CodeScan::declarationsIn(blanked, body);
        QCOMPARE(declarations.size(), 4);
        QCOMPARE(declarations[0].text, QString("void a()"));
        QCOMPARE(declarations[0].access, QString("public"));
        QCOMPARE(declarations[1].access, QString("signals"));
        QVERIFY(declarations[2].hasBody);
        QCOMPARE(declarations[3].text, QString("struct Inner"));
        QCOMPARE(CodeScan::blank("a = \"{\"; // }\nb").count('{'), 0);
    }

    void keywordsFitTheirPlace()
    {
        const auto at = [](const QByteArray &text, const QString &languageId) {
            return KeywordProvider::keywordsAt(languageId, text, int(text.size()));
        };
        // Java: declarations at the top, modifiers in a class body,
        // statements in a method.
        const QStringList top = at("package a;\n", "java");
        QVERIFY(top.contains("import") && top.contains("class"));
        QVERIFY(!top.contains("return") && !top.contains("int"));
        const QStringList member = at("class A {\n    ", "java");
        QVERIFY(member.contains("private") && member.contains("void") && member.contains("static"));
        QVERIFY(!member.contains("return") && !member.contains("if") && !member.contains("import"));
        const QStringList code = at("class A {\n    void f() {\n        ", "java");
        QVERIFY(code.contains("return") && code.contains("if") && code.contains("int"));
        QVERIFY(!code.contains("private") && !code.contains("import"));
        // An initializer is code, a class body or not.
        QVERIFY(at("class A {\n    int x = ", "java").contains("new"));
        QVERIFY(at("class A {\n    Runnable r = new Runnable() {\n        ", "java").contains("public"));
        // JavaScript: a class body's members.
        const QStringList js = at("class A {\n  ", "javascript");
        QVERIFY(js.contains("static") && js.contains("constructor") && js.contains("async"));
        QVERIFY(!js.contains("private") && !js.contains("if")); // TypeScript's, a statement
        QVERIFY(at("class A {\n  ", "typescript").contains("private"));
        QVERIFY(at("function f() {\n  ", "javascript").contains("return"));
        QVERIFY(!at("function f() {\n  ", "typescript").contains("private"));
        // Others: all of them.
        QCOMPARE(at("int main() {\n", "cpp"), KeywordProvider::keywords("cpp"));
    }

    void javaPostfixTemplatesImportWhatTheyUse()
    {
        const Fixture f("package app;\n\nimport java.util.List;\n\nclass A {\n    void f() {\n        name\n    }\n}\n",
                        {6, 12}, std::make_shared<PostfixTemplateProvider>(), "A.java");
        QTest::keyClicks(f.editor, ".opt");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels().first(), QString("opt"));
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(2), QString("import java.util.List;\n"));
        QCOMPARE(f.editor->text(3), QString("import java.util.Optional;\n"));
        QCOMPARE(f.editor->text(7), QString("        Optional.ofNullable(name)\n"));
        f.editor->undo(); // one step, the import too
        QCOMPARE(f.editor->text(3), QString("\n"));
        QCOMPARE(f.editor->text(6), QString("        name.opt\n"));

        // Imported already: once.
        const Fixture g("import java.util.*;\nclass A {\n    void f() {\n        items\n    }\n}\n", {3, 13},
                        std::make_shared<PostfixTemplateProvider>(), "A.java");
        QTest::keyClicks(g.editor, ".stream");
        QTRY_VERIFY(g.controller->isActive());
        QTest::keyClick(g.editor, Qt::Key_Return);
        QCOMPARE(g.editor->text(0), QString("import java.util.*;\n"));
        QCOMPARE(g.editor->text(3), QString("        Arrays.stream(items)\n"));
    }

    void javaSoutvQuotesTheExpression()
    {
        const Fixture f("class A {\n    void f() {\n        map.get(\"a\")\n    }\n}\n", {2, 20},
                        std::make_shared<PostfixTemplateProvider>(), "A.java");
        QTest::keyClicks(f.editor, ".soutv");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels().first(), QString("soutv"));
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(2),
                 QString("        System.out.println(\"map.get(\\\"a\\\") = \" + map.get(\"a\"));\n"));
    }

    void javaLiveTemplatesKnowWhereTheyAre()
    {
        const QByteArray text =
            "class Account {\n    void deposit(long amount, String note) {\n        int total = 3;\n"
            "        \n    }\n}\n";
        const auto expand = [&text](const QString &key) {
            Fixture f(text, {3, 8}, std::make_shared<LiveTemplateProvider>(), "Account.java");
            QTest::keyClicks(f.editor, key);
            if (!QTest::qWaitFor([&] { return f.controller->isActive(); }))
                return QStringLiteral("(no list)");
            if (f.controller->popup()->labels().first() != key)
                return QStringLiteral("(first: %1)").arg(f.controller->popup()->labels().first());
            QTest::keyClick(f.editor, Qt::Key_Return);
            return f.editor->text(3);
        };
        QCOMPARE(expand("soutm"), QString("        System.out.println(\"Account.deposit\");\n"));
        QCOMPARE(expand("soutp"),
                 QString("        System.out.println(\"amount = \" + amount + \", note = \" + note);\n"));
        QCOMPARE(expand("soutv"), QString("        System.out.println(\"total = \" + total);\n"));
        QCOMPARE(expand("geti").left(48), QString("        public static Account getInstance() {\n"));
    }

    void javaGettersAndSettersFromFields()
    {
        const QByteArray text = "class Person {\n    private String name;\n    private final int age = 3;\n"
                                "    private boolean active, admin;\n    public String getName() { return name; }\n"
                                "    \n}\n";
        const auto provider = std::make_shared<CodeGenerationProvider>(nullptr, CodeGenerationProvider::Counterpart());
        CompletionContext context;
        context.languageId = "java";
        context.filePath = "Person.java";
        context.text = text;
        context.position = context.prefixStart = int(text.indexOf("    \n}")) + 4;
        QStringList generated;
        provider->complete(context, [&generated](const QList<CompletionItem> &items, bool) {
            for (const CompletionItem &item : items)
                generated.append(item.label);
        });
        QVERIFY2(generated.contains("getAge") && generated.contains("isActive") && generated.contains("setName"),
                 qPrintable(generated.join(", ")));
        QVERIFY(!generated.contains("getName"));   // written already
        QVERIFY(!generated.contains("getActive")); // "isActive" for a boolean
        QVERIFY(!generated.contains("setAge"));    // final

        const Fixture f(text, {5, 4}, provider, "Person.java");
        QTest::keyClicks(f.editor, "getAg");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels().first(), QString("getAge"));
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(5), QString("    public int getAge() {\n"));
        QCOMPARE(f.editor->text(6), QString("        return age;\n"));
        QCOMPARE(f.editor->text(7), QString("    }\n"));

        f.editor->setCursorPosition(8, 0);
        QTest::keyClicks(f.editor, "    setAdm");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels().first(), QString("setAdmin"));
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(8), QString("    public void setAdmin(boolean admin) {\n"));
        QCOMPARE(f.editor->text(9), QString("        this.admin = admin;\n"));
    }

    void modulePathsOnlyAskTheServer()
    {
        QVERIFY(CodeScan::inModulePath("import { x } from './co", 23));
        QVERIFY(CodeScan::inModulePath("const fs = require('f", 21));
        QVERIFY(CodeScan::inModulePath("import 'polyfill", 16));
        QVERIFY(CodeScan::inModulePath("const m = await import(\"./", 26));
        QVERIFY(!CodeScan::inModulePath("const s = 'from", 15));
        QVERIFY(!CodeScan::inModulePath("import { x } from './a';\nconst s = '", 37));

        // Only a provider serving module paths is asked there.
        class Paths : public RecordingProvider {
        public:
            using RecordingProvider::RecordingProvider;

            bool servesModulePaths() const override { return true; }
        };

        const auto paths =
            std::make_shared<Paths>(QList<CompletionItem>{{"components", {}, {}, CompletionItem::Kind::Module, 10}});
        const Fixture f("import Button from '';\n", {0, 20}, paths, "a.js");
        f.controller->complete();
        QTRY_VERIFY(f.controller->isActive());
        QVERIFY(paths->last.inModulePath);
        QCOMPARE(paths->last.triggerCharacter, QChar('\''));
        QCOMPARE(f.controller->popup()->labels(), QStringList({"components"})); // no keywords, no words
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(0), QString("import Button from 'components';\n"));
        // Typing in it opens the list too.
        f.controller->close();
        f.editor->setCursorPosition(0, 30);
        QTest::keyClicks(f.editor, "/");
        QTRY_COMPARE(paths->calls, 2);
        QVERIFY(paths->last.inModulePath);
        QCOMPARE(paths->last.triggerCharacter, QChar('/'));
    }

    void overridesFromTheBaseClass()
    {
        const QByteArray text =
            "class Base {\npublic:\n    virtual ~Base();\n    virtual void paint(int x) const;\n"
            "    virtual int size() const = 0;\n    void plain();\n    virtual void done() final;\n};\n"
            "class Derived : public Base {\npublic:\n    \n};\n";
        const auto provider = std::make_shared<CodeGenerationProvider>(nullptr, CodeGenerationProvider::Counterpart());
        const Fixture f(text, {10, 4}, provider);
        QTest::keyClicks(f.editor, "pai");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels().first(), QString("paint"));
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(10), QString("    void paint(int x) const override\n"));
        QCOMPARE(f.editor->text(11), QString("    {\n"));
        QCOMPARE(f.editor->text(12), QString("        Base::paint(x);\n"));
        QCOMPARE(f.editor->text(13), QString("    }\n"));
        QTest::keyClick(f.editor, Qt::Key_End);
        QTest::keyClick(f.editor, Qt::Key_Down);
        QTest::keyClick(f.editor, Qt::Key_Return);
        QTest::keyClicks(f.editor, "s");
        f.controller->complete();
        QTRY_VERIFY(f.controller->isActive());
        const QStringList labels = f.controller->popup()->labels();
        QVERIFY(labels.contains("size"));
        QVERIFY(!labels.contains("paint")); // overridden now
        QVERIFY(!labels.contains("plain")); // not virtual
        f.controller->close();
        QTest::keyClicks(f.editor, "d");
        f.controller->complete();
        QTRY_VERIFY(f.controller->isActive());
        QVERIFY(!f.controller->popup()->labels().contains("done")); // final
        // In a header: the declaration only.
        const Fixture h(text, {10, 4}, provider, "a.h");
        QTest::keyClicks(h.editor, "si");
        QTRY_VERIFY(h.controller->isActive());
        QCOMPARE(h.controller->popup()->labels().first(), QString("size"));
        QTest::keyClick(h.editor, Qt::Key_Return);
        QCOMPARE(h.editor->text(10), QString("    int size() const override;\n"));
        // Java.
        const QByteArray java = "interface Shape {\n    double area();\n    static Shape unit() { return null; }\n}\n"
                                "class Circle implements Shape {\n    \n}\n";
        const Fixture j(java, {5, 4}, provider, "Circle.java");
        QTest::keyClicks(j.editor, "are");
        QTRY_VERIFY(j.controller->isActive());
        QCOMPARE(j.controller->popup()->labels().first(), QString("area"));
        QTest::keyClick(j.editor, Qt::Key_Return);
        QCOMPARE(j.editor->text(5), QString("    @Override\n"));
        QCOMPARE(j.editor->text(6), QString("    public double area() {\n"));
        j.controller->close();
        j.editor->setCursorPosition(9, 0);
        QTest::keyClicks(j.editor, "toS");
        QTRY_VERIFY(j.controller->isActive());
        QVERIFY(j.controller->popup()->labels().contains("toString")); // Object's
        QVERIFY(!j.controller->popup()->labels().contains("unit"));    // static
    }

    void implementationsFromTheHeader()
    {
        const QTemporaryDir headers;
        QFile header(headers.filePath("a.h"));
        QVERIFY(header.open(QIODevice::WriteOnly));
        header.write(
            "class Foo : public QObject {\n    Q_OBJECT\npublic:\n    explicit Foo(int n = 0);\n"
            "    virtual int size() const override;\n    void clear();\n    int inlined() const { return 0; }\n"
            "    virtual void abstract() = 0;\nsignals:\n    void changed();\n};\nint helper(double x);\n");
        header.close();
        const auto provider = std::make_shared<CodeGenerationProvider>(
            nullptr, [&headers](const QString &) { return headers.filePath("a.h"); });
        const Fixture f("#include \"a.h\"\n\nvoid Foo::clear()\n{\n}\n\n", {5, 0}, provider);
        QTest::keyClicks(f.editor, "si");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels().first(), QString("size"));
        QTest::keyClick(f.editor, Qt::Key_Escape);
        QTest::keyClick(f.editor, Qt::Key_Backspace);
        QTest::keyClick(f.editor, Qt::Key_Backspace);
        f.controller->complete();
        QTRY_VERIFY(f.controller->isActive());
        const QStringList labels = f.controller->popup()->labels();
        QVERIFY(labels.contains("Foo")); // the constructor
        QVERIFY(labels.contains("helper"));
        QCOMPARE(labels.count("clear"), 1);    // defined already: the word of the file only
        QVERIFY(!labels.contains("inlined"));  // has a body
        QVERIFY(!labels.contains("abstract")); // pure
        QVERIFY(!labels.contains("changed"));  // a signal: moc writes it
        f.controller->close();
        QTest::keyClicks(f.editor, "Foo::si");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels(), QStringList({"size"}));
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(5), QString("int Foo::size() const\n"));
        QCOMPARE(f.editor->text(6), QString("{\n"));
        QCOMPARE(f.editor->text(8), QString("}\n"));
        int line = 0;
        int index = 0;
        f.editor->getCursorPosition(&line, &index);
        QCOMPARE(line, 7);
    }

    void namesFromTheDeclaredType()
    {
        using P = NameSuggestionProvider;
        QCOMPARE(P::namesFor("QListWidget", "cpp"), QStringList({"listWidget", "widget"}));
        QCOMPARE(P::namesFor("std::vector<Item>", "cpp").first(), QString("items"));
        QCOMPARE(P::namesFor("QList<Entry *>", "cpp").first(), QString("entries"));
        QCOMPARE(P::namesFor("std::unique_ptr<HttpRequest>", "cpp"), QStringList({"httpRequest", "request"}));
        QCOMPARE(P::namesFor("HTMLParser", "cpp"), QStringList({"htmlParser", "parser"}));
        QCOMPARE(P::namesFor("Box[]", "java").first(), QString("boxes"));
        QCOMPARE(P::namesFor("my_type_t", "c"), QStringList({"my_type", "type"}));
        QCOMPARE(P::namesFor("int", "cpp").first(), QString("i"));
        QCOMPARE(P::declaredTypeAt("    const Widget *", 17, "cpp"), QString("Widget"));
        QCOMPARE(P::declaredTypeAt("void f(std::map<K, V> ", 22, "cpp"), QString("std::map<K, V>"));
        QCOMPARE(P::declaredTypeAt("    return Foo ", 15, "cpp"), QString());
        QCOMPARE(P::declaredTypeAt("    foo ", 8, "cpp"), QString());    // not a type
        QCOMPARE(P::declaredTypeAt("    Widget", 10, "cpp"), QString()); // the type itself
        QCOMPARE(P::declaredTypeAt("    private final Widget ", 25, "java"), QString("Widget"));

        const Fixture f("int main() {\n    QListWidget \n}\n", {1, 16}, std::make_shared<NameSuggestionProvider>());
        QTest::keyClicks(f.editor, "w");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels().first(), QString("widget"));
        QTest::keyClick(f.editor, Qt::Key_Backspace);
        f.controller->complete();
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels().mid(0, 2), QStringList({"listWidget", "widget"}));
    }

    void parameterInfoAfterAFunction()
    {
        const auto late = std::make_shared<LateProvider>();
        const Fixture f("int main() {\n    \n}\n", {1, 4}, late);
        const QSignalSpy calls(f.controller, &CompletionController::callInserted);
        QTest::keyClicks(f.editor, "pri");
        QTRY_VERIFY(late->pending);
        CompletionItem print{"print", "print(${1:int a})", {}, CompletionItem::Kind::Function, 10};
        print.isSnippet = true;
        const CompletionItem variable{"printed", {}, {}, CompletionItem::Kind::Variable, 9};
        late->pending({print, variable}, false);
        QTRY_VERIFY(f.controller->isActive());
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(calls.count(), 1);
        QTest::keyClick(f.editor, Qt::Key_End);
        QTest::keyClick(f.editor, Qt::Key_Return);
        QTest::keyClicks(f.editor, "pri");
        QTRY_VERIFY(late->pending);
        late->pending({print, variable}, false);
        QTRY_VERIFY(f.controller->isActive());
        QTest::keyClick(f.editor, Qt::Key_Down);
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(calls.count(), 1); // a variable: no call
    }

    void postfixChoosesTheExpression()
    {
        const auto starts = [](const QByteArray &text) {
            const int dot = int(text.lastIndexOf('.'));
            QList<QByteArray> result;
            for (const int at : PostfixTemplateProvider::expressionStarts(text, dot))
                result.append(text.mid(at, dot - at));
            return result;
        };
        QCOMPARE(starts("    a + b."), QList<QByteArray>({"b", "a + b"}));
        QCOMPARE(starts("x = n * f(y) - z."), QList<QByteArray>({"z", "f(y) - z", "n * f(y) - z"}));
        QCOMPARE(starts("    ready && !done."), QList<QByteArray>({"done", "!done", "ready && !done"}));
        QCOMPARE(starts("total += b."), QList<QByteArray>({"b"}));
        QCOMPARE(starts("return a.b."), QList<QByteArray>({"a.b"}));

        Fixture f("int main() {\n    a + b\n}\n", {1, 9});
        f.service.addProvider(std::make_shared<PostfixTemplateProvider>());
        // "if" only fits the whole: no choice.
        QTest::keyClicks(f.editor, ".if");
        QTRY_VERIFY(f.controller->isActive());
        QTest::keyClick(f.editor, Qt::Key_Return);
        QCOMPARE(f.editor->text(1), QString("    if (a + b) {\n"));
        f.editor->undo();
        QTest::keyClick(f.editor, Qt::Key_End);
        for (int i = 0; i < 3; ++i)
            QTest::keyClick(f.editor, Qt::Key_Backspace);
        QCOMPARE(f.editor->text(1), QString("    a + b\n"));
        // "not": "b" or "a + b".
        QTest::keyClicks(f.editor, ".not");
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels().first(), QString("not"));
        QTest::keyClick(f.editor, Qt::Key_Return);
        QVERIFY(f.controller->isChoosing());
        QCOMPARE(f.controller->popup()->labels(), QStringList({"b", "a + b"}));
        QVERIFY(f.controller->popup()->keyHint().startsWith("Expression to wrap"));
        QCOMPARE(f.editor->text(1), QString("    a + b.not\n")); // nothing inserted yet
        QTest::keyClick(f.editor, Qt::Key_Down);
        QTest::keyClick(f.editor, Qt::Key_Return);
        QVERIFY(!f.controller->isActive());
        QCOMPARE(f.editor->text(1), QString("    !(a + b)\n"));
        f.editor->undo();
        QCOMPARE(f.editor->text(1), QString("    a + b.not\n"));
        // Escape gives the choice up.
        QTest::keyClick(f.editor, Qt::Key_End);
        for (int i = 0; i < 3; ++i)
            QTest::keyClick(f.editor, Qt::Key_Backspace);
        QTest::keyClicks(f.editor, "par");
        QTRY_VERIFY(f.controller->isActive());
        QTest::keyClick(f.editor, Qt::Key_Return);
        QVERIFY(f.controller->isChoosing());
        QTest::keyClick(f.editor, Qt::Key_Escape);
        QVERIFY(!f.controller->isActive());
        QCOMPARE(f.editor->text(1), QString("    a + b.par\n"));
    }

private:
    static int cursorLine(EditorWidget *editor)
    {
        int line = 0;
        int index = 0;
        editor->getCursorPosition(&line, &index);
        return line;
    }

    // An editor showing a C++ file, with completion, the cursor at
    // the given position.
    // Where the cursor starts in a fixture's editor.
    struct Cursor {
        int line = 0;
        int index = 0;
    };

    struct Fixture {
        Fixture(const QByteArray &text,
                Cursor cursor,
                const std::shared_ptr<ICompletionProvider> &extra = {},
                const QString &name = QStringLiteral("a.cpp"))
        {
            QFile file(dir.filePath(name));
            if (!file.open(QIODevice::WriteOnly))
                qFatal("cannot write");
            file.write(text);
            file.close();
            if (extra)
                service.addProvider(extra);
            service.addProvider(std::make_shared<KeywordProvider>());
            service.addProvider(std::make_shared<DocumentWordsProvider>());
            editor = new EditorWidget(Theme::dark(), &window);
            editor->load(dir.filePath(name), nullptr);
            controller = new CompletionController(editor, &service);
            window.resize(800, 600);
            editor->resize(800, 600);
            window.show();
            if (!QTest::qWaitForWindowExposed(&window))
                qFatal("no window");
            editor->setFocus();
            editor->setCursorPosition(cursor.line, cursor.index);
        }

        QTemporaryDir dir;
        CompletionService service; // outlives the editor, destroyed with the window
        QWidget window;
        EditorWidget *editor = nullptr;
        CompletionController *controller = nullptr;
    };
};

QTEST_MAIN(TestCompletion)
#include "tst_completion.moc"
