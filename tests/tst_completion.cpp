#include "completion/CompletionController.h"
#include "completion/CompletionPopup.h"
#include "completion/CompletionService.h"
#include "completion/FuzzyMatch.h"
#include "completion/LocalCompletion.h"
#include "completion/Snippet.h"
#include "editor/EditorWidget.h"

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
