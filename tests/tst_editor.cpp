// SPDX-License-Identifier: GPL-3.0-only

#include "editor/ChangeGutter.h"
#include "editor/ClangFormatStyle.h"
#include "editor/CodeColorizer.h"
#include "editor/CodeFormatter.h"
#include "editor/EditorStatus.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "editor/FileWatcher.h"
#include "editor/FindBar.h"
#include "editor/MarkdownHtml.h"
#include "editor/PreviewPage.h"
#include "editor/PreviewSite.h"
#include "editor/ScrollBarMarks.h"
#include "editor/SymbolOccurrences.h"
#include "git/GitProcess.h"
#include "workspace/ChoicePopup.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextBlock>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <Qsci/qscicommandset.h>
#include <QtTest>

namespace {

void write(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QString selected(const EditorWidget &editor)
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
        const QTemporaryDir dir;
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
        const QTemporaryDir dir;
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
        int line = 0;
        int index = 0;
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
        const QTemporaryDir dir;
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
        const QTemporaryDir dir;
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

    void contextMenuOffersTheFileActionsOnlyForAFile()
    {
        const QTemporaryDir dir;
        write(dir.filePath("a.txt"), "one\n");
        QAction explain(QStringLiteral("Explain This Code"));
        EditorWidget editor(Theme::dark());
        editor.setFileActions({&explain});

        QVERIFY(!editor.createContextMenu()->actions().contains(&explain));
        QVERIFY(editor.load(dir.filePath("a.txt"), nullptr));
        QCOMPARE(editor.createContextMenu()->actions().value(0), &explain);
    }

    void contextMenuLeavesUndoAndRedoToTheirShortcuts()
    {
        const QTemporaryDir dir;
        write(dir.filePath("a.txt"), "one\n");
        QAction rename(QStringLiteral("Rename…"));
        EditorWidget editor(Theme::dark());
        editor.setFileActions({&rename});
        QVERIFY(editor.load(dir.filePath("a.txt"), nullptr));

        const std::unique_ptr<QMenu> menu = editor.createContextMenu();

        QStringList texts;
        for (const QAction *action : menu->actions())
            texts.append(action->text());
        QVERIFY2(!texts.contains("&Undo") && !texts.contains("&Redo"), qPrintable(texts.join(", ")));
        QCOMPARE(texts.value(0), QString("Rename…"));
        QVERIFY(menu->actions().value(1)->isSeparator());
        QCOMPARE(texts.value(2), QString("Cu&t"));
    }

    void contextMenuSuspendsTheToolTipsWhileOpen()
    {
        EditorWidget editor(Theme::dark());
        editor.setText("int value;\n");
        editor.setHoverHelp([](int) { return QStringLiteral("Help"); });
        editor.resize(400, 200);
        editor.show();
        editor.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&editor));
        bool suspendedWhileOpen = false;
        bool toolTipWhileOpen = true;
        QTimer::singleShot(0, &editor, [&] {
            suspendedWhileOpen = editor.areToolTipsSuspended();
            emit editor.SCN_DWELLSTART(0, 10, 10);
            toolTipWhileOpen = QToolTip::isVisible();
            if (QWidget *menu = QApplication::activePopupWidget())
                menu->close();
        });

        QContextMenuEvent rightClick(QContextMenuEvent::Mouse, QPoint(10, 10),
                                     editor.viewport()->mapToGlobal(QPoint(10, 10)));
        QApplication::sendEvent(editor.viewport(), &rightClick);

        QVERIFY(suspendedWhileOpen);
        QVERIFY(!toolTipWhileOpen);
        QVERIFY(!editor.areToolTipsSuspended()); // back once it closes
    }

    void highlightsEveryOccurrenceOfTheNameAtTheCursor()
    {
        EditorWidget editor(Theme::dark());
        editor.setText("int count = 0;\ncount += 1; // count\nint counter;\n");
        SymbolOccurrences *occurrences = editor.symbolOccurrences();

        editor.setCursorPosition(1, 2);
        occurrences->refreshNow();

        // Whole words, the comment's too, but not "counter".
        using Range = SymbolOccurrences::Range;
        QCOMPARE(occurrences->occurrences(), (QList<Range>{{4, 9}, {15, 20}, {30, 35}}));
        const auto *marks = editor.verticalScrollBar()->findChild<ScrollBarMarks *>();
        QVERIFY(marks);
        QCOMPARE(marks->marks().size(), 3);
    }

    void clearsTheOccurrencesOffAName()
    {
        EditorWidget editor(Theme::dark());
        editor.setText("int count = 0;\nint x = 42;\n");
        SymbolOccurrences *occurrences = editor.symbolOccurrences();
        editor.setCursorPosition(0, 5);
        occurrences->refreshNow();
        QCOMPARE(occurrences->occurrences().size(), 1);

        editor.setCursorPosition(1, 9); // in a number
        occurrences->refreshNow();
        QVERIFY(occurrences->occurrences().isEmpty());

        editor.setSelection(0, 0, 0, 9); // more than a word
        occurrences->refreshNow();
        QVERIFY(occurrences->occurrences().isEmpty());

        editor.setSelection(0, 4, 0, 9); // the word, as a double click selects it
        occurrences->refreshNow();
        QCOMPARE(occurrences->occurrences().size(), 1);

        editor.insertAt("// ", 0, 0); // the ranges no longer hold
        QVERIFY(occurrences->occurrences().isEmpty());
    }

    void serverOccurrencesReplaceTheTextOnes()
    {
        EditorWidget editor(Theme::dark());
        editor.setText("int count;\nint f() { int count; return count; }\n");
        SymbolOccurrences *occurrences = editor.symbolOccurrences();
        QSignalSpy settled(occurrences, &SymbolOccurrences::nameSettled);
        editor.setCursorPosition(0, 5);
        occurrences->refreshNow();
        QCOMPARE(occurrences->occurrences().size(), 3);
        QCOMPARE(settled.size(), 1);
        const int position = settled.first().first().toInt();
        using Range = SymbolOccurrences::Range;

        occurrences->showServerOccurrences(position + 1, {}); // another name's answer
        QCOMPARE(occurrences->occurrences().size(), 3);
        occurrences->showServerOccurrences(position, {{4, 9}});
        QCOMPARE(occurrences->occurrences(), (QList<Range>{{4, 9}}));

        // Back on the same name: the server's occurrences stay.
        editor.setCursorPosition(0, 6);
        occurrences->refreshNow();
        QCOMPARE(occurrences->occurrences(), (QList<Range>{{4, 9}}));
    }

    void showsTheMarkOnlyWhenNoFileIsOpen()
    {
        const QTemporaryDir dir;
        write(dir.filePath("empty.txt"), "");
        EditorTabs tabs(Theme::dark());
        tabs.resize(600, 400);
        const auto markIsDrawn = [&tabs] {
            const QImage image = tabs.grab().toImage();
            const QRect center(image.width() / 3, image.height() / 3, image.width() / 3, image.height() / 3);
            const QRgb background = image.pixel(center.topLeft());
            for (int y = center.top(); y <= center.bottom(); ++y) {
                for (int x = center.left(); x <= center.right(); ++x) {
                    if (image.pixel(x, y) != background)
                        return true;
                }
            }
            return false;
        };

        QVERIFY(markIsDrawn());
        QVERIFY(tabs.openFile(dir.filePath("empty.txt")));
        QVERIFY(!markIsDrawn());
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
        QVERIFY(html.contains("<h1 dir=\"auto\" data-line=\"1\"><a id=\"title\" class=\"anchor\""));
        QVERIFY(html.contains("<h2 dir=\"auto\" data-line=\"3\"><a id=\"title-1\" class=\"anchor\""));
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

        const QString page = MarkdownHtml::page(html);
        QVERIFY(page.contains("color-scheme: light"));
        QVERIFY(page.contains("<article class=\"markdown-body\">"));
        QVERIFY(page.contains("Content-Security-Policy"));
        QCOMPARE(MarkdownHtml::prettyLightsClass("function.method", "cpp"), QString("pl-en"));
        QCOMPARE(MarkdownHtml::prettyLightsClass("property", "json"), QString("pl-ent"));
        QCOMPARE(MarkdownHtml::prettyLightsClass("punctuation", "cpp"), QString());
    }

    void markdownTabsShowEditorAndPreview()
    {
        const QTemporaryDir dir;
        write(dir.filePath("README.md"), "# Hello\n");
        QSettings().setValue("markdown/mode", 2); // Preview
        EditorTabs tabs(Theme::dark());
        auto *page = qobject_cast<PreviewPage *>(tabs.openFile(dir.filePath("README.md")));
        QVERIFY(page);
        QCOMPARE(page->mode(), PreviewPage::Mode::Preview);
        QVERIFY(!tabs.tabIcon(0).isNull()); // the file type's icon
        // The tab counts as an editor everywhere.
        QCOMPARE(tabs.currentEditor(), page->editor());
        QCOMPARE(tabs.editorFor(dir.filePath("README.md")), page->editor());
        QCOMPARE(tabs.currentFilePath(), dir.filePath("README.md"));
        QCOMPARE(tabs.openFile(dir.filePath("README.md")), page);
        // No web engine until the page shows.
        QVERIFY(!page->markdownPreview());
        tabs.show();
        QTRY_VERIFY(page->markdownPreview());
        QTRY_VERIFY(page->markdownPreview()->bodyHtml().contains("Hello"));

        // The preview follows the text.
        page->editor()->setText("# Changed\n");
        QTRY_VERIFY(page->markdownPreview()->bodyHtml().contains("Changed"));
        QCOMPARE(tabs.tabText(0), QString("● README.md"));
        QVERIFY(tabs.saveCurrent());
        QCOMPARE(tabs.tabText(0), QString("README.md"));

        tabs.revealEditor(page->editor()); // a search or a jump: the editor shows
        QCOMPARE(page->mode(), PreviewPage::Mode::Split);
        page->findChildren<QToolButton *>().first()->click(); // Editor
        QCOMPARE(page->mode(), PreviewPage::Mode::Editor);
        QCOMPARE(QSettings().value("markdown/mode").toInt(), 0); // kept for the next files
        QSettings().remove("markdown/mode");
    }

    void previewShowsOnceItsPageIsDrawn()
    {
        const QTemporaryDir dir;
        write(dir.filePath("README.md"), "# Hello\n");
        QSettings().setValue("markdown/mode", 2); // Preview
        EditorTabs tabs(Theme::dark());
        const auto *page = qobject_cast<PreviewPage *>(tabs.openFile(dir.filePath("README.md")));

        tabs.show();

        QVERIFY(page->markdownPreview());
        QVERIFY(!page->markdownPreview()->isVisible()); // an empty web view flashes
        QTRY_VERIFY(page->markdownPreview()->isVisible());
        QVERIFY(page->markdownPreview()->bodyHtml().contains("Hello"));
        QSettings().remove("markdown/mode");
    }

    void editorStaysHiddenWhileThePreviewIsCreated()
    {
        const QTemporaryDir dir;
        write(dir.filePath("README.md"), "# Hello\n");
        QSettings().setValue("markdown/mode", 2); // Preview
        EditorTabs tabs(Theme::dark());
        const auto *page = qobject_cast<PreviewPage *>(tabs.openFile(dir.filePath("README.md")));

        // Creating the first web view repaints the window: the editor must
        // not be there to show.
        QVERIFY(!page->editor()->isVisibleTo(page));
        tabs.show();

        QVERIFY(page->markdownPreview());
        QVERIFY(!page->editor()->isVisible());
        QSettings().remove("markdown/mode");
    }

    void htmlTabsShowThePage()
    {
        const QTemporaryDir dir;
        write(dir.filePath("index.html"), "<html><head><title>Page</title></head>"
                                          "<body><h1>Hello</h1><script>document.title = 'ran';</script></body></html>");
        EditorTabs tabs(Theme::dark());
        const auto *page = qobject_cast<PreviewPage *>(tabs.openFile(dir.filePath("index.html")));
        QVERIFY(page);
        QCOMPARE(page->kind(), PreviewPage::Kind::Html);
        QCOMPARE(page->mode(), PreviewPage::Mode::Split); // the code beside the page
        QCOMPARE(tabs.currentEditor(), page->editor());
        QVERIFY(!page->htmlPreview());
        tabs.show();
        QTRY_VERIFY(page->htmlPreview());
        const HtmlPreview *preview = page->htmlPreview();
        QTRY_VERIFY(preview->isReady());
        const auto pageText = [preview] {
            QString text;
            bool done = false;
            preview->page()->toPlainText([&](const QString &result) {
                text = result;
                done = true;
            });
            for (int i = 0; i < 100 && !done; ++i)
                QTest::qWait(50);
            return text;
        };
        QVERIFY(pageText().contains("Hello"));
        QTRY_COMPARE(preview->title(), QString("ran")); // its scripts run

        // The page follows the text.
        page->editor()->setText("<html><body><p>Changed</p></body></html>");
        QTRY_VERIFY(preview->isReady() && pageText().contains("Changed"));
        QVERIFY(!page->markdownPreview());
        QSettings().remove("html/mode");
    }

    void htmlPagesLoadTheProjectFilesButNoOther()
    {
        const QTemporaryDir dir;
        const QString project = dir.filePath("project");
        write(dir.filePath("secret.txt"), "secret");
        write(project + "/data.txt", "inside");
        write(project + "/scripts/answer.js", "export const answer = 42;");
        write(project + "/site/index.html",
              "<html><body><script type=\"module\">"
              "import {answer} from '../scripts/answer.js';"
              "const read = path => fetch(path).then(r => r.ok ? r.text() : 'refused', () => 'refused');"
              "const texts = await Promise.all([read('../data.txt'), read('../../secret.txt'),"
              "                                 read('" +
                  QUrl::fromLocalFile(dir.filePath("secret.txt")).toString().toUtf8() +
                  "')]);"
                  "document.title = [answer, ...texts].join(' ');"
                  "</script></body></html>");
        QSettings().setValue("html/mode", 2); // Preview
        EditorTabs tabs(Theme::dark());
        tabs.setProjectFolder(project);
        const auto *page = qobject_cast<PreviewPage *>(tabs.openFile(project + "/site/index.html"));

        tabs.show();

        QTRY_VERIFY(page->htmlPreview());
        QTRY_COMPARE(page->htmlPreview()->title(), QString("42 inside refused refused"));
        QSettings().remove("html/mode");
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
        const auto programs = [](const QString &languageId) {
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

    void clangFormatStyleIsReadFromTheDumpedConfig()
    {
        const QByteArray dumped =
            "---\nLanguage:        Cpp\nBraceWrapping:\n  IndentWidth: 9\n"
            "ColumnLimit:     100\nIndentWidth:     3\nTabWidth:        8\nUseTab:          Never\n";
        const std::optional<ClangFormatStyle::Style> style = ClangFormatStyle::parse(dumped);
        QVERIFY(style);
        QCOMPARE(style->indentWidth, 3); // the top-level key, not the nested one
        QCOMPARE(style->tabWidth, 8);
        QCOMPARE(style->columnLimit, 100);
        QVERIFY(!style->useTabs);
        QVERIFY(ClangFormatStyle::parse("UseTab: ForIndentation\nIndentWidth: 4\n")->useTabs);
        QVERIFY(!ClangFormatStyle::parse("error: invalid configuration\n"));
    }

    void editorIndentsAsTheProjectClangFormat()
    {
        if (QStandardPaths::findExecutable("clang-format").isEmpty())
            QSKIP("clang-format is not installed");
        const QTemporaryDir dir;
        write(dir.filePath(".clang-format"), "IndentWidth: 3\nColumnLimit: 90\n");
        write(dir.filePath("main.cpp"), "int main()\n{\n}\n");
        write(dir.filePath("notes.txt"), "text\n");
        EditorTabs tabs(Theme::dark());
        tabs.openFile(dir.filePath("main.cpp"));
        const EditorWidget *code = tabs.currentEditor();
        QTRY_COMPARE(code->tabWidth(), 3);
        QVERIFY(!code->indentationsUseTabs());
        QCOMPARE(code->edgeColumn(), 90);
        QCOMPARE(code->edgeMode(), QsciScintilla::EdgeLine);

        // Files clang-format does not format keep the default.
        tabs.openFile(dir.filePath("notes.txt"));
        QCOMPARE(tabs.currentEditor()->edgeMode(), QsciScintilla::EdgeNone);

        // Saving the .clang-format restyles the open files.
        tabs.openFile(dir.filePath(".clang-format"));
        EditorWidget *config = tabs.currentEditor();
        config->setText("IndentWidth: 2\nColumnLimit: 0\n");
        QVERIFY(tabs.saveCurrent());
        QTRY_COMPARE(code->tabWidth(), 2);
        QCOMPARE(code->edgeMode(), QsciScintilla::EdgeNone);
    }

    void formattedTextKeepsLineEndingAndUndoesAtOnce()
    {
        const QTemporaryDir dir;
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
        const QTemporaryDir dir;
        write(dir.filePath(".clang-format"), "BasedOnStyle: LLVM\nIndentWidth: 4\n");
        write(dir.filePath("a.cpp"), "int f(){return 1;}\n");
        EditorTabs tabs(Theme::dark());
        QVERIFY(tabs.openFile(dir.filePath("a.cpp")));
        QSignalSpy messages(&tabs, &EditorTabs::statusMessage);

        // The formatter runs without blocking the editor.
        tabs.formatCurrent();
        QTRY_COMPARE(messages.size(), 1);
        QCOMPARE(tabs.currentEditor()->text(), QString("int f() { return 1; }\n"));
        QCOMPARE(messages.last().first().toString(), QString("a.cpp formatted"));
        tabs.formatCurrent();
        QTRY_COMPARE(messages.size(), 2);
        QCOMPARE(messages.last().first().toString(), QString("a.cpp is already formatted"));

        // Format on save: the file on disk is the formatted text.
        tabs.currentEditor()->setText("int g(){return 2;}\n");
        tabs.setFormatOnSave(true);
        const QSignalSpy saved(&tabs, &EditorTabs::fileSaved);
        QVERIFY(tabs.saveCurrent());
        QTRY_COMPARE(saved.size(), 1);
        QFile file(dir.filePath("a.cpp"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("int g() { return 2; }\n"));
        file.close();

        // Typing while the formatter runs: the typed text is kept, and saved.
        tabs.currentEditor()->setText("int h(){return 3;}\n");
        QVERIFY(tabs.saveCurrent());
        tabs.currentEditor()->setText("int h(){return 4;}\n");
        QTRY_COMPARE(saved.size(), 2);
        QCOMPARE(tabs.currentEditor()->text(), QString("int h(){return 4;}\n"));
    }

    void savingALatin1FileWithAnEuroWritesItInUtf8()
    {
        const QTemporaryDir dir;
        write(dir.filePath("a.txt"), "caf\xE9\n");
        EditorWidget editor(Theme::dark());
        QVERIFY(editor.load(dir.filePath("a.txt"), nullptr));
        QVERIFY(editor.format().isLatin1);
        const QSignalSpy formatChanged(&editor, &EditorWidget::formatChanged);

        editor.setText(QStringLiteral("café 5 €\n"));
        QVERIFY(editor.save(nullptr));

        QFile file(dir.filePath("a.txt"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QStringLiteral("café 5 €\n").toUtf8());
        QVERIFY(!editor.format().isLatin1);
        QCOMPARE(formatChanged.size(), 1);
    }

    void reloadKeepsTheTextAfterANulByte()
    {
        const QTemporaryDir dir;
        // Past the 8 KiB the binary check reads.
        const QByteArray start(10000, 'a');
        write(dir.filePath("a.txt"), start + "\n");
        EditorWidget editor(Theme::dark());
        QVERIFY(editor.load(dir.filePath("a.txt"), nullptr));

        const QByteArray changed = start + QByteArray("\0end\n", 5);
        write(dir.filePath("a.txt"), changed);
        QVERIFY(editor.reloadFromDisk(nullptr));

        QCOMPARE(QByteArray(editor.utf8Bytes()), changed);
    }

    void saveAllNowWritesTheFormattedFilesBeforeReturning()
    {
        if (QStandardPaths::findExecutable("clang-format").isEmpty())
            QSKIP("clang-format is not installed");
        const QTemporaryDir dir;
        write(dir.filePath(".clang-format"), "BasedOnStyle: LLVM\n");
        write(dir.filePath("a.cpp"), "int f();\n");
        EditorTabs tabs(Theme::dark());
        QVERIFY(tabs.openFile(dir.filePath("a.cpp")));
        tabs.setFormatOnSave(true);
        tabs.currentEditor()->setText("int g(){return 2;}\n");

        QVERIFY(tabs.saveAllNow());

        QFile file(dir.filePath("a.cpp"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("int g() { return 2; }\n"));
    }

    void closingATabAfterItsQuestionClosesNoOtherTab()
    {
        const QTemporaryDir dir;
        write(dir.filePath("a.txt"), "a\n");
        write(dir.filePath("b.txt"), "b\n");
        EditorTabs tabs(Theme::dark());
        QVERIFY(tabs.openFile(dir.filePath("a.txt")));
        QVERIFY(tabs.openFile(dir.filePath("b.txt")));
        tabs.currentEditor()->setText("changed\n");

        // While "Save changes to b.txt?" is open, a.txt's tab is closed
        // (deleted on disk), then the user discards b.txt's changes.
        QTimer::singleShot(0, &tabs, [&tabs, &dir] {
            tabs.closeFilesUnder(dir.filePath("a.txt"));
            auto *question = tabs.findChild<ChoicePopup *>();
            QVERIFY(question);
            question->choiceButtons().at(1)->click(); // Discard
        });
        tabs.closeCurrent();

        QCOMPARE(tabs.count(), 0);
    }

    void asksOnceToReloadEachVersionOnDisk()
    {
        const QTemporaryDir dir;
        const QString path = dir.filePath("a.txt");
        write(path, "a\n");
        EditorTabs tabs(Theme::dark());
        QVERIFY(tabs.openFile(path));
        tabs.currentEditor()->setText("mine\n");
        int questionCount = 0;
        QTimer keepMyChanges;
        connect(&keepMyChanges, &QTimer::timeout, &tabs, [&tabs, &questionCount] {
            for (ChoicePopup *question : tabs.findChildren<ChoicePopup *>()) {
                if (question->property("answered").toBool())
                    continue; // deleted once its event loop runs again
                question->setProperty("answered", true);
                ++questionCount;
                question->cancelButton()->click();
            }
        });
        keepMyChanges.start(10);

        write(path, "theirs\n");
        tabs.syncWithDisk(path);
        tabs.syncWithDisk(path); // an agent's next command
        write(path, "theirs again\n");
        tabs.syncWithDisk(path);

        QCOMPARE(questionCount, 2);
        QCOMPARE(tabs.currentEditor()->text(), QStringLiteral("mine\n"));
    }

    void watchesAFileDeletedThenCreatedAgain()
    {
        const QTemporaryDir dir;
        const QString path = dir.filePath("a.txt");
        write(path, "a\n");
        FileWatcher watcher;
        watcher.watch(path);
        const QSignalSpy changed(&watcher, &FileWatcher::fileChanged);

        QVERIFY(QFile::remove(path));
        QTRY_COMPARE(changed.size(), 1);
        write(path, "b\n");
        QTRY_COMPARE(changed.size(), 2);
        write(path, "c\n");

        QTRY_COMPARE(changed.size(), 3);
    }

    void formatDocumentReportsAMissingFormatter()
    {
        const QTemporaryDir dir;
        write(dir.filePath("notes.txt"), "text\n");
        EditorTabs tabs(Theme::dark());
        QVERIFY(tabs.openFile(dir.filePath("notes.txt")));
        QSignalSpy messages(&tabs, &EditorTabs::statusMessage);
        tabs.formatCurrent();
        QCOMPARE(messages.last().first().toString(), QString("No formatter for notes.txt"));
    }

    void foldsTheBlockAroundTheCursor()
    {
        const QTemporaryDir dir;
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
        QVERIFY(visible(3));       // the closing brace stays
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
    PreviewSite::registerScheme();
    const QApplication app(argc, argv);
    TestEditor test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_editor.moc"
