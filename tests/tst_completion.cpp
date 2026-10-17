#include "completion/CompletionController.h"
#include "completion/CompletionHistory.h"
#include "completion/CompletionPopup.h"
#include "completion/CompletionService.h"
#include "completion/FuzzyMatch.h"
#include "completion/LocalCompletion.h"
#include "completion/Snippet.h"
#include "completion/SnippetSession.h"
#include "completion/Templates.h"
#include "editor/EditorWidget.h"

#include <QAccessible>
#include <QListView>
#include <QTemporaryDir>
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
        QCOMPARE(ranked("gv", {"getSelectedValue", "setValue", "getValue"}), QStringList({"getValue", "getSelectedValue"}));
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
        auto late = std::make_shared<LateProvider>();
        Fixture f("class A {\n    void f() {\n        \n    }\n}\n", 2, 8, late);
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
        f.editor->undo(); // one step for both
        QCOMPARE(f.editor->text(2), QString("        Arr\n"));
        QVERIFY(!f.editor->text().contains("import"));
    }

    void editsResolvedOnceChosen()
    {
        auto late = std::make_shared<LateProvider>();
        Fixture f("class A {\n    void f() {\n        \n    }\n}\n", 2, 8, late);
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
        Fixture f("int printValue(int value);\nint main() {\n    \n}\n", 2, 4);
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
        Fixture f("int printValue;\nint process;\nint main() {\n    \n}\n", 3, 4);
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

    void modesDecideWhenThePopupOpens()
    {
        {
            Fixture f("int printValue;\nint main() {\n    \n}\n", 2, 4);
            f.editor->setOptions({.autoCompletion = AutoCompletion::Off});
            QTest::keyClicks(f.editor, "pri");
            QTest::qWait(150);
            QVERIFY(!f.controller->isActive());
            f.controller->complete(); // Ctrl+Space still opens it
            QTRY_VERIFY(f.controller->isActive());
        }
        {
            Fixture f("int printValue;\nint main() {\n    \n}\n", 2, 4);
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
        Fixture f("int printValue;\nint main() {\n    \n}\n", 2, 4);
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
        Fixture f("int getSelectedValue;\nint main() {\n    \n}\n", 2, 4);
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
        Fixture f("int printValue;\nint main() {\n    \n    x = 1;\n}\n", 2, 4);
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
        auto late = std::make_shared<LateProvider>();
        Fixture f("int main() {\n    \n}\n", 1, 4, late);
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
        // Ctrl+Space hides it, and shows it again.
        f.controller->complete();
        QVERIFY(popup->documentationText().isEmpty());
        f.controller->complete();
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
        Fixture f("int printValue;\nint process;\nint main() {\n    \n}\n", 3, 4);
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
        QCOMPARE(history.boost("java", "value", now), 0); // per language
        history.recordRejected("cpp", "wrong");
        history.recordRejected("cpp", "wrong");
        QVERIFY(history.boost("cpp", "wrong", now) < 0);

        // Kept in its file.
        QTemporaryDir dir;
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
        Fixture f("int printAlpha;\nint printBeta;\nint main() {\n    \n}\n", 3, 4);
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
        QBENCHMARK {
            popup.setItems(items, QStringLiteral("gsv"));
        }
        QCOMPARE(popup.count(), 200);
    }

    void collectsDocumentWords()
    {
        QByteArray text;
        for (int i = 0; i < 20000; ++i)
            text += "    const int value" + QByteArray::number(i % 500) + " = compute(first, second);\n";
        QBENCHMARK {
            DocumentWordsProvider::words(text, int(text.size() / 2));
        }
    }

    void tabReplacesTheWholeIdentifier()
    {
        Fixture f("int printValue;\nint x = prinOld;\n", 1, 11);
        f.controller->complete();
        QTRY_VERIFY(f.controller->isActive());
        QCOMPARE(f.controller->popup()->labels().first(), QString("printValue"));
        QTest::keyClick(f.editor, Qt::Key_Tab);
        QCOMPARE(f.editor->text(1), QString("int x = printValue;\n"));
    }

    void noPopupInCommentsOrStrings()
    {
        Fixture f("int printValue;\n// \nconst char *s = \"\";\n", 1, 3);
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
        auto late = std::make_shared<LateProvider>();
        Fixture f("int printValue;\nint main() {\n    \n}\n", 2, 4, late);
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
        auto late = std::make_shared<LateProvider>();
        Fixture f("int main() {\n    \n}\n", 1, 4, late);
        QTest::keyClicks(f.editor, "lo");
        QTRY_VERIFY(late->pending);
        CompletionItem loop{"loop", "for (int ${1:i} = 0; $1 < ${2:n}; ++$1) {$0}", {}, CompletionItem::Kind::Snippet, 10};
        loop.isSnippet = true;
        late->pending({loop}, false);
        QTRY_VERIFY(f.controller->isActive());
        QTest::keyClick(f.editor, Qt::Key_Return);
        SnippetSession *snippet = f.controller->snippet();
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
        auto late = std::make_shared<LateProvider>();
        Fixture f("int main() {\n    \n}\n", 1, 4, late);
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
        Fixture f("int main() {\n    items\n}\n", 1, 9);
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
        Fixture f("int main() {\n    widget.getSelectedItem()\n}\n", 1, 28);
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
        Fixture f("int main() {\n    int n = items\n}\n", 1, 17);
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
        Fixture f("int main() {\n    \n}\n", 1, 4);
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

private:
    // An editor showing a C++ file, with completion, the cursor at
    // (line, index).
    struct Fixture {
        Fixture(const QByteArray &text, int line, int index, std::shared_ptr<ICompletionProvider> extra = {})
        {
            QFile file(dir.filePath("a.cpp"));
            if (!file.open(QIODevice::WriteOnly))
                qFatal("cannot write");
            file.write(text);
            file.close();
            if (extra)
                service.addProvider(extra);
            service.addProvider(std::make_shared<KeywordProvider>());
            service.addProvider(std::make_shared<DocumentWordsProvider>());
            editor = new EditorWidget(Theme::dark(), &window);
            editor->load(dir.filePath("a.cpp"), nullptr);
            controller = new CompletionController(editor, &service);
            window.resize(800, 600);
            editor->resize(800, 600);
            window.show();
            if (!QTest::qWaitForWindowExposed(&window))
                qFatal("no window");
            editor->setFocus();
            editor->setCursorPosition(line, index);
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
