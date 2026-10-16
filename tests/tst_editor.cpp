#include "editor/ChangeGutter.h"
#include "editor/CodeColorizer.h"
#include "editor/CodeFormatter.h"
#include "editor/EditorStatus.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "editor/FindBar.h"
#include "editor/MarkdownHtml.h"
#include "editor/MarkdownView.h"
#include "git/GitProcess.h"

#include <QLineEdit>
#include <Qsci/qscicommandset.h>
#include <QTextBlock>
#include <QToolButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest>

namespace {

void write(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QString selected(EditorWidget &editor)
{
    return editor.selectedText();
}

} // namespace

class TestEditor : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(config_.open());
        qputenv("GIT_CONFIG_GLOBAL", config_.fileName().toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_AUTHOR_NAME", "t");
        qputenv("GIT_AUTHOR_EMAIL", "t@t");
        qputenv("GIT_COMMITTER_NAME", "t");
        qputenv("GIT_COMMITTER_EMAIL", "t@t");
    }

    void findCountsAndCyclesThroughMatches()
    {
        EditorWidget editor(Theme::dark());
        editor.setText("foo bar Foo\nfoobar foo\n");
        FindBar bar;
        bar.setEditor(&editor);
        bar.showFind();
        bar.searchField()->setText("foo");
        QCOMPARE(bar.matchCount(), 4); // case insensitive by default
        QCOMPARE(bar.currentMatch(), 1);
        bar.findNext();
        QCOMPARE(bar.currentMatch(), 2);
        QCOMPARE(selected(editor), QString("Foo"));
        bar.findPrevious();
        QCOMPARE(bar.currentMatch(), 1);
        bar.findPrevious(); // wraps around
        QCOMPARE(bar.currentMatch(), 4);

        bar.setOptions(true, false, false);
        QCOMPARE(bar.matchCount(), 3);
        bar.setOptions(false, true, false);
        QCOMPARE(bar.matchCount(), 3); // not "foobar"

        // Typing follows the text.
        editor.append("foo");
        bar.findNext();
        QCOMPARE(bar.matchCount(), 4);
    }

    void findReportsInvalidExpressions()
    {
        EditorWidget editor(Theme::dark());
        editor.setText("a1 b22 c333\n");
        FindBar bar;
        bar.setEditor(&editor);
        bar.showFind();
        bar.setOptions(false, false, true);
        bar.searchField()->setText("[0-9]+");
        QCOMPARE(bar.matchCount(), 3);
        QVERIFY(bar.isPatternValid());
        bar.searchField()->setText("([0-9");
        QVERIFY(!bar.isPatternValid());
        QCOMPARE(bar.matchCount(), 0);
    }

    void replaceOneThenAllWithGroups()
    {
        EditorWidget editor(Theme::dark());
        editor.setText("x=1; y=2; z=3;\n");
        FindBar bar;
        bar.setEditor(&editor);
        bar.showReplace();
        bar.setOptions(false, false, true);
        bar.searchField()->setText("(\\w)=(\\d)");
        bar.replaceField()->setText("\\2=\\1");
        bar.replaceCurrent();
        QCOMPARE(editor.text(), QString("1=x; y=2; z=3;\n"));
        QCOMPARE(selected(editor), QString("y=2")); // the next one is selected
        bar.replaceAll();
        QCOMPARE(editor.text(), QString("1=x; 2=y; 3=z;\n"));
        // A single undo step.
        editor.undo();
        QCOMPARE(editor.text(), QString("1=x; y=2; z=3;\n"));
    }

    void statusDescribesTheFile()
    {
        QTemporaryDir dir;
        write(dir.filePath("crlf.txt"), "été\r\n\tx = 1\r\n");
        EditorWidget editor(Theme::dark());
        QString error;
        QVERIFY(editor.load(dir.filePath("crlf.txt"), &error));
        EditorStatus status;
        status.setEditor(&editor);
        QCOMPARE(status.lineEndingText(), QString("CRLF"));
        QCOMPARE(status.encodingText(), QString("UTF-8"));
        QCOMPARE(status.indentationText(), QString("4 spaces"));
        editor.setCursorPosition(0, 3); // after "été": characters, not bytes
        QCOMPARE(status.positionText(), QString("1:4"));
        editor.setSelection(0, 0, 1, 1);
        QCOMPARE(status.positionText(), QString("2:2 (5 chars, 1 line break)"));

        editor.setLineEnding(LineEnding::Lf);
        QCOMPARE(status.lineEndingText(), QString("LF"));
        QVERIFY(!editor.text().contains('\r'));
        editor.setIndentationStyle(true, 4);
        QCOMPARE(status.indentationText(), QString("Tab"));
    }

    void parseLineColumn()
    {
        QCOMPARE(EditorStatus::parseLineColumn("12"), (std::pair{11, 0}));
        QCOMPARE(EditorStatus::parseLineColumn(" 3:7 "), (std::pair{2, 6}));
        QCOMPARE(EditorStatus::parseLineColumn("3:"), (std::pair{2, 0}));
        QVERIFY(!EditorStatus::parseLineColumn("0"));
        QVERIFY(!EditorStatus::parseLineColumn("x"));
        QVERIFY(!EditorStatus::parseLineColumn("1:2:3"));
    }

    void gutterMarksAndRollsBackChanges()
    {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("git unavailable");
        QTemporaryDir dir;
        write(dir.filePath("a.txt"), "one\ntwo\nthree\nfour\n");
        Git::run(dir.path(), {"init", "-q"});
        Git::run(dir.path(), {"add", "a.txt"});

        EditorWidget editor(Theme::dark());
        QString error;
        QVERIFY(editor.load(dir.filePath("a.txt"), &error));
        ChangeGutter *gutter = editor.changeGutter();
        QVERIFY(gutter->hasBase());
        QVERIFY(gutter->changes().isEmpty());

        // Unsaved edits are compared too.
        editor.setText("one\nTWO\nthree\nfour\nfive\n");
        QList<LineChange> changes = gutter->changes();
        QCOMPARE(changes.size(), 2);
        QCOMPARE(changes[0].kind(), LineChange::Kind::Modified);
        QCOMPARE(changes[1].kind(), LineChange::Kind::Added);
        QVERIFY(gutter->changeAt(1));
        QVERIFY(!gutter->changeAt(0));
        QVERIFY(gutter->diffText(changes[0]).contains("-two\n+TWO\n"));

        editor.setCursorPosition(0, 0);
        QVERIFY(gutter->goToNext());
        int line = 0, index = 0;
        editor.getCursorPosition(&line, &index);
        QCOMPARE(line, 1);
        QVERIFY(gutter->goToNext());
        editor.getCursorPosition(&line, &index);
        QCOMPARE(line, 4);
        QVERIFY(gutter->goToNext()); // wraps around
        editor.getCursorPosition(&line, &index);
        QCOMPARE(line, 1);

        QVERIFY(gutter->rollbackAtCursor());
        QCOMPARE(editor.text(), QString("one\ntwo\nthree\nfour\nfive\n"));
        gutter->rollback(gutter->changes().first());
        QCOMPARE(editor.text(), QString("one\ntwo\nthree\nfour\n"));
        QVERIFY(gutter->changes().isEmpty());
        editor.undo(); // a rollback is undone like an edit
        QCOMPARE(editor.text(), QString("one\ntwo\nthree\nfour\nfive\n"));

        // Deleted lines.
        editor.setText("one\nfour\n");
        changes = gutter->changes();
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes[0].kind(), LineChange::Kind::Deleted);
        QVERIFY(gutter->changeAt(1));
        gutter->rollback(changes[0]);
        QCOMPARE(editor.text(), QString("one\ntwo\nthree\nfour\n"));
    }

    void gutterWithoutRepositoryHasNoMarks()
    {
        QTemporaryDir dir;
        write(dir.filePath("a.txt"), "one\n");
        EditorWidget editor(Theme::dark());
        QString error;
        QVERIFY(editor.load(dir.filePath("a.txt"), &error));
        QVERIFY(!editor.changeGutter()->hasBase());
        editor.setText("two\n");
        QVERIFY(editor.changeGutter()->changes().isEmpty());
        QVERIFY(!editor.changeGutter()->goToNext());
    }

    void showDiffOpensATab()
    {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("git unavailable");
        QTemporaryDir dir;
        write(dir.filePath("a.txt"), "one\n");
        Git::run(dir.path(), {"init", "-q"});
        Git::run(dir.path(), {"add", "a.txt"});
        EditorTabs tabs(Theme::dark());
        auto *editor = qobject_cast<EditorWidget *>(tabs.openFile(dir.filePath("a.txt")));
        QVERIFY(editor);
        editor->setText("two\n");
        const LineChange change = editor->changeGutter()->changes().first();
        emit editor->changeGutter()->diffRequested("Change", editor->changeGutter()->diffText(change));
        QCOMPARE(tabs.count(), 2);
        QCOMPARE(tabs.unsavedTexts().value(dir.filePath("a.txt")), QString("two\n"));
    }

    void codeCategoriesComeFromTheEditor()
    {
        QCOMPARE(CodeColorizer::languageOfFence("cpp"), QString("cpp"));
        QCOMPARE(CodeColorizer::languageOfFence("C++"), QString("cpp"));
        QCOMPARE(CodeColorizer::languageOfFence("python title=x"), QString("python"));
        QCOMPARE(CodeColorizer::languageOfFence("ts"), QString("typescript"));
        QCOMPARE(CodeColorizer::languageOfFence("cmake"), QString("cmake"));
        QCOMPARE(CodeColorizer::languageOfFence("klingon"), QString("text"));
        QCOMPARE(CodeColorizer::languageOfFence(""), QString("text"));

        CodeColorizer colorizer;
        const QString code = "// é\nint x = 42;";
        const QList<CodeColorizer::Span> spans = colorizer.categorize(code, "cpp");
        const auto categoryAt = [&spans](int position) {
            for (const CodeColorizer::Span &span : spans) {
                if (position >= span.start && position < span.start + span.length)
                    return span.category;
            }
            return QString();
        };
        // Positions in characters, past the "é" (two bytes).
        QCOMPARE(categoryAt(0), QString("comment"));
        QVERIFY(categoryAt(code.indexOf("int")).startsWith("type") || categoryAt(code.indexOf("int")) == "keyword");
        QCOMPARE(categoryAt(code.indexOf("42")), QString("number"));
        QVERIFY(colorizer.categorize(code, "text").isEmpty());
        // Lexer languages too.
        QVERIFY(!colorizer.categorize("{ \"a\": 1 }", "json").isEmpty());
    }

    void markdownHtmlIsGitHubs()
    {
        QCOMPARE(MarkdownHtml::headingAnchor("Getting Started!"), QString("getting-started"));
        QCOMPARE(MarkdownHtml::headingAnchor("  Go to File / Symbol "), QString("go-to-file--symbol"));
        QCOMPARE(MarkdownHtml::headingAnchor("Ctrl+E (récents)"), QString("ctrle-récents"));

        CodeColorizer colorizer;
        const QString html = MarkdownHtml::body("# Title\n\n## Title\n\n"
                                                "Some `code`, ~~gone~~, https://example.com and a [link](other.md).\n\n"
                                                "```cpp\nint x; // note\n```\n\n```\nplain <b>\n```\n\n"
                                                "| A | B |\n|---|:-:|\n| 1 | 2 |\n\n"
                                                "- [x] done\n- [ ] todo\n\n"
                                                "> [!WARNING]\n> Careful.\n\n> Just a quote.\n\n"
                                                "Note[^1].\n\n[^1]: The footnote.\n\n"
                                                "<script>alert(1)</script>\n",
                                                &colorizer);
        // Headings: GitHub's anchors, numbered when repeated.
        QVERIFY(html.contains("<h1 dir=\"auto\"><a id=\"title\" class=\"anchor\""));
        QVERIFY(html.contains("<a id=\"title-1\" class=\"anchor\""));
        QVERIFY(html.contains("octicon-link"));
        // GFM extensions.
        QVERIFY(html.contains("<code>code</code>"));
        QVERIFY(html.contains("<del>gone</del>"));
        QVERIFY(html.contains("<a href=\"https://example.com\">"));
        QVERIFY(html.contains("<table>"));
        QVERIFY(html.contains("align=\"center\""));
        // Code: GitHub's highlight markup and classes; unknown language plain.
        QVERIFY(html.contains("<div class=\"highlight highlight-source-cpp"));
        QVERIFY(html.contains("<span class=\"pl-c\">// note</span>"));
        QVERIFY(html.contains("<pre><code>plain &lt;b&gt;</code></pre>"));
        // Task lists, alerts, footnotes.
        QVERIFY(html.contains("<ul class=\"contains-task-list\">"));
        QVERIFY(html.contains("class=\"task-list-item-checkbox\" checked=\"\""));
        QVERIFY(html.contains("<div class=\"markdown-alert markdown-alert-warning\""));
        QVERIFY(html.contains("Warning</p><p>Careful.</p>"));
        QVERIFY(!html.contains("[!WARNING]"));
        QVERIFY(html.contains("<blockquote>\n<p>Just a quote.</p>"));
        QVERIFY(html.contains("footnotes"));
        // The tag filter: no script.
        QVERIFY(!html.contains("<script>"));

        const QString dark = MarkdownHtml::page(html, true);
        QVERIFY(dark.contains("color-scheme: dark"));
        QVERIFY(dark.contains("<article class=\"markdown-body\">"));
        QVERIFY(dark.contains("Content-Security-Policy"));
        QVERIFY(MarkdownHtml::page(html, false).contains("color-scheme: light"));
        QCOMPARE(MarkdownHtml::prettyLightsClass("function.method", "cpp"), QString("pl-en"));
        QCOMPARE(MarkdownHtml::prettyLightsClass("property", "json"), QString("pl-ent"));
        QCOMPARE(MarkdownHtml::prettyLightsClass("punctuation", "cpp"), QString());
    }

    void markdownTabsShowEditorAndPreview()
    {
        QTemporaryDir dir;
        write(dir.filePath("README.md"), "# Hello\n");
        QSettings().setValue("markdown/mode", 2); // Preview
        EditorTabs tabs(Theme::dark());
        auto *page = qobject_cast<MarkdownPage *>(tabs.openFile(dir.filePath("README.md")));
        QVERIFY(page);
        QCOMPARE(page->mode(), MarkdownPage::Mode::Preview);
        QVERIFY(!tabs.tabIcon(0).isNull()); // the file type's icon
        // The tab counts as an editor everywhere.
        QCOMPARE(tabs.currentEditor(), page->editor());
        QCOMPARE(tabs.editorFor(dir.filePath("README.md")), page->editor());
        QCOMPARE(tabs.currentFilePath(), dir.filePath("README.md"));
        QCOMPARE(tabs.openFile(dir.filePath("README.md")), page);
        QVERIFY(page->preview()->bodyHtml().contains("Hello"));

        // The preview follows the text.
        page->editor()->setText("# Changed\n");
        QTRY_VERIFY(page->preview()->bodyHtml().contains("Changed"));
        QCOMPARE(tabs.tabText(0), QString("● README.md"));
        QVERIFY(tabs.saveCurrent());
        QCOMPARE(tabs.tabText(0), QString("README.md"));

        tabs.revealEditor(page->editor()); // a search or a jump: the editor shows
        QCOMPARE(page->mode(), MarkdownPage::Mode::Split);
        page->findChildren<QToolButton *>().first()->click(); // Editor
        QCOMPARE(page->mode(), MarkdownPage::Mode::Editor);
        QCOMPARE(QSettings().value("markdown/mode").toInt(), 0); // kept for the next files
        QSettings().remove("markdown/mode");
    }

    void formatterPositionsFollowTheCode()
    {
        const QByteArray before = "int  f( int a ){\n  return a;}\n";
        const QByteArray after = "int f(int a) {\n    return a;\n}\n";
        // Before "return": still before it, after the new indentation.
        QCOMPARE(CodeFormatter::mapPosition(before, before.indexOf("return"), after), after.indexOf("return"));
        // Before the brace that moves to its own line: it follows the brace.
        QCOMPARE(CodeFormatter::mapPosition(before, before.indexOf("a;}") + 2, after), after.lastIndexOf('}'));
        // At the end of a line: after the same code.
        QCOMPARE(CodeFormatter::mapPosition(before, before.indexOf("{\n") + 1, after), after.indexOf("{\n") + 1);
        QCOMPARE(CodeFormatter::mapPosition(before, 0, after), 0);
        QCOMPARE(CodeFormatter::mapPosition(before, before.size(), after), after.size() - 1);
        // Never inside a UTF-8 character.
        QCOMPARE(CodeFormatter::mapPosition("x = \"é\";", 7, "x=\"e\xcc\x81\";"), 4);
    }

    void formatterCandidatesDependOnTheLanguage()
    {
        auto programs = [](const QString &languageId) {
            QStringList names;
            for (const CodeFormatter::Command &command : CodeFormatter::candidates(languageId, "/p/file"))
                names.append(command.program);
            return names;
        };
        QCOMPARE(programs("cpp"), QStringList{"clang-format"});
        QCOMPARE(programs("typescript"), (QStringList{"prettier", "clang-format"}));
        QCOMPARE(programs("python"), (QStringList{"ruff", "black"}));
        QVERIFY(programs("text").isEmpty());
        QCOMPARE(CodeFormatter::describeCandidates("json", "/p/a.json"), QString("prettier or clang-format"));
        QCOMPARE(CodeFormatter::candidates("cpp", "/p/a.cpp").first().arguments,
                 QStringList{"--assume-filename=/p/a.cpp"});
    }

    void formattedTextKeepsLineEndingAndUndoesAtOnce()
    {
        QTemporaryDir dir;
        write(dir.filePath("a.cpp"), "int  x;\r\nint y;\r\n");
        EditorWidget editor(Theme::dark());
        QVERIFY(editor.load(dir.filePath("a.cpp"), nullptr));
        editor.setCursorPosition(1, 4); // on "y"

        QVERIFY(editor.applyFormattedText("int x;\nint y;\n"));
        QCOMPARE(editor.text(), QString("int x;\r\nint y;\r\n"));
        int line = 0;
        int index = 0;
        editor.getCursorPosition(&line, &index);
        QCOMPARE(line, 1);
        QCOMPARE(index, 4);
        QVERIFY(!editor.applyFormattedText("int x;\r\nint y;\r\n"));

        editor.undo();
        QCOMPARE(editor.text(), QString("int  x;\r\nint y;\r\n"));
    }

    void formatDocumentRunsClangFormat()
    {
        if (QStandardPaths::findExecutable("clang-format").isEmpty())
            QSKIP("clang-format is not installed");
        QTemporaryDir dir;
        write(dir.filePath(".clang-format"), "BasedOnStyle: LLVM\nIndentWidth: 4\n");
        write(dir.filePath("a.cpp"), "int f(){return 1;}\n");
        EditorTabs tabs(Theme::dark());
        QVERIFY(tabs.openFile(dir.filePath("a.cpp")));
        QSignalSpy messages(&tabs, &EditorTabs::statusMessage);

        tabs.formatCurrent();
        QCOMPARE(tabs.currentEditor()->text(), QString("int f() { return 1; }\n"));
        QCOMPARE(messages.last().first().toString(), QString("a.cpp formatted"));
        tabs.formatCurrent();
        QCOMPARE(messages.last().first().toString(), QString("a.cpp is already formatted"));

        // Format on save: the file on disk is the formatted text.
        tabs.currentEditor()->setText("int g(){return 2;}\n");
        tabs.setFormatOnSave(true);
        QVERIFY(tabs.saveCurrent());
        QFile file(dir.filePath("a.cpp"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("int g() { return 2; }\n"));
    }

    void formatDocumentReportsAMissingFormatter()
    {
        QTemporaryDir dir;
        write(dir.filePath("notes.txt"), "text\n");
        EditorTabs tabs(Theme::dark());
        QVERIFY(tabs.openFile(dir.filePath("notes.txt")));
        QSignalSpy messages(&tabs, &EditorTabs::statusMessage);
        tabs.formatCurrent();
        QCOMPARE(messages.last().first().toString(), QString("No formatter for notes.txt"));
    }

    void foldsTheBlockAroundTheCursor()
    {
        QTemporaryDir dir;
        write(dir.filePath("a.cpp"), "int f() {\n    int a;\n    int b;\n}\nint g() {\n    int c;\n    int d;\n}\n");
        EditorWidget editor(Theme::dark());
        QVERIFY(editor.load(dir.filePath("a.cpp"), nullptr));
        editor.SendScintilla(QsciScintillaBase::SCI_COLOURISE, 0L, -1L);
        const auto visible = [&editor](int line) {
            return editor.SendScintilla(QsciScintillaBase::SCI_GETLINEVISIBLE, line) != 0;
        };
        const auto cursorLine = [&editor] {
            int line = 0;
            int index = 0;
            editor.getCursorPosition(&line, &index);
            return line;
        };

        editor.setCursorPosition(2, 4); // in f
        editor.foldBlock(true);
        QVERIFY(!visible(1) && !visible(2));
        QVERIFY(visible(3)); // the closing brace stays
        QCOMPARE(cursorLine(), 0); // never on a hidden line
        editor.foldBlock(false);
        QVERIFY(visible(1) && visible(2));

        editor.foldEverything(true);
        QVERIFY(!visible(1) && !visible(5));
        // A jump into a folded block opens it (once the editor updates).
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        editor.setCursorPosition(5, 4);
        QTRY_VERIFY(visible(5));
        QVERIFY(!visible(1));

        // Without folding, nothing stays hidden.
        editor.setOptions({.codeFolding = false});
        QVERIFY(visible(1));
        editor.foldBlock(true);
        QVERIFY(visible(1));
    }

    void menuShortcutsWinOverQScintilla()
    {
        EditorWidget editor(Theme::dark());
        const auto bound = [&editor](const char *shortcut) {
            return editor.standardCommands()->boundTo(QKeySequence(shortcut)[0].toCombined()) != nullptr;
        };
        QVERIFY(bound("Ctrl+-")); // QScintilla's zoom out
        EditorWidget::setReservedKeys({QKeySequence("Ctrl+-"), QKeySequence("Ctrl+K, Ctrl+O")});
        QVERIFY(!bound("Ctrl+-"));
        EditorWidget::setReservedKeys({});
        QVERIFY(bound("Ctrl+-")); // given back
        EditorWidget::setReservedKeys({QKeySequence("Ctrl+B"), QKeySequence("Alt+Left"), QKeySequence("Alt+Right")});
    }

private:
    QTemporaryFile config_;
};

// Qt WebEngine (Markdown preview) needs this before the application exists.
int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    TestEditor test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_editor.moc"
