// SPDX-License-Identifier: GPL-3.0-only

#include "diagnostics/ProblemStore.h"
#include "editor/CodeColorizer.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "editor/MarkdownToolTip.h"
#include "editor/SymbolOccurrences.h"
#include "index/OccurrenceIndex.h"
#include "lsp/LanguageServers.h"
#include "lsp/LspClient.h"
#include "lsp/LspProtocol.h"
#include "navigation/CodeIntelligence.h"
#include "navigation/CodeNavigator.h"
#include "navigation/SymbolIndex.h"
#include "navigation/WorkspaceEditing.h"
#include "refactor/RefactoringHistory.h"
#include "refactor/RefactoringPreviewPopup.h"
#include "refactor/RenameSymbolPopup.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QDir>
#include <QJsonArray>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolTip>
#include <QtTest>

using namespace LspProtocol;

namespace {

void write(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QJsonObject range(int line, int start, int end)
{
    return toJson(Range{{line, start}, {line, end}});
}

// Just after the start of `before` in `editor`.
void placeCaretIn(const EditorWidget *editor, const QByteArray &before)
{
    const qsizetype found = editor->utf8Bytes().toByteArray().indexOf(before);
    QVERIFY(found >= 0);
    editor->SendScintilla(QsciScintillaBase::SCI_GOTOPOS, static_cast<unsigned long>(found + 1));
}

int caretLine(EditorWidget *editor)
{
    return CodeIntelligence::lspPosition(editor, int(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS))).line;
}

struct ClangdProject {
    QTemporaryDir dir;
    EditorTabs tabs{Theme::kodamiko()};
    SymbolIndex index;
    LanguageServers servers;
    CodeNavigator navigator{&tabs, &index};
    ProblemStore problems{&servers};
    OccurrenceIndex occurrences;
    CodeIntelligence intelligence{{&tabs, &servers, &navigator, &problems, &occurrences.store()}};
    QStringList messages;

    ClangdProject()
    {
        QStandardPaths::setTestModeEnabled(true); // the index's cache
        index.setFolder(dir.path());
        servers.setFolder(dir.path());
        write(dir.filePath("compile_flags.txt"), "-std=c++20\n");
        QObject::connect(&tabs, &EditorTabs::editorOpened, &intelligence, &CodeIntelligence::attach);
        QObject::connect(&intelligence, &CodeIntelligence::statusMessage,
                         [this](const QString &message) { messages.append(message); });
        tabs.show();
    }

    ~ClangdProject() { QFile::remove(OccurrenceIndex::cachePathFor(dir.path())); }

    ClangdProject(const ClangdProject &) = delete;
    ClangdProject &operator=(const ClangdProject &) = delete;

    // Writes `name` with `content`, opens it, and waits for clangd.
    EditorWidget *open(const QString &name, const QByteArray &content)
    {
        write(dir.filePath(name), content);
        auto *editor = EditorTabs::editorIn(tabs.openFile(dir.filePath(name)));
        LspClient *client = servers.clientFor("cpp");
        if (!editor || !client)
            return nullptr;
        servers.syncDocument(client, editor->filePath(), "cpp", editor->utf8Bytes().toByteArray());
        if (!QTest::qWaitFor([client] { return client->isReady(); }, 30000))
            return nullptr;
        const QSignalSpy indexed(&occurrences, &OccurrenceIndex::indexingFinished);
        occurrences.setFolder(dir.path());
        if (!QTest::qWaitFor([&indexed] { return !indexed.isEmpty(); }, 30000))
            return nullptr;
        return editor;
    }
};

// The occurrence index of `project`, once built.
void indexProject(ClangdProject &project)
{
    const QSignalSpy indexed(&project.occurrences, &OccurrenceIndex::indexingFinished);
    project.occurrences.setFolder(project.dir.path());
    QTRY_VERIFY(!indexed.isEmpty());
}

// "total" renamed to "sum" by a refactoring, in open.h, open, and in
// closed.h, not; open.h's editor, or nullptr if the rename failed.
EditorWidget *openRenamedInTwoFiles(ClangdProject &project)
{
    write(project.dir.filePath("open.h"), "int total;\n");
    write(project.dir.filePath("closed.h"), "int total;\n");
    auto *editor = EditorTabs::editorIn(project.tabs.openFile(project.dir.filePath("open.h")));
    const LspProtocol::Range total{{0, 4}, {0, 9}};
    const LspProtocol::WorkspaceEdit edit{{
        LspProtocol::DocumentEdit{project.dir.filePath("open.h"), {}, {{total, "sum"}}},
        LspProtocol::DocumentEdit{project.dir.filePath("closed.h"), {}, {{total, "sum"}}},
    }};
    if (!editor || project.intelligence.applyWorkspaceEdit(edit, "Rename “total” to “sum”") != 2)
        return nullptr;
    return editor;
}

} // namespace

class TestCodeIntelligence : public QObject {
    Q_OBJECT

private slots:

    void parsesLocationsAndLinks()
    {
        const QString uri = uriOf("/p/a.cpp");
        QCOMPARE(parseLocations(QJsonObject{{"uri", uri}, {"range", range(2, 4, 7)}}),
                 (QList<Location>{{"/p/a.cpp", {{2, 4}, {2, 7}}}}));
        const QJsonArray links{QJsonObject{{"targetUri", uriOf("/p/b.h")},
                                           {"targetRange", range(0, 0, 30)},
                                           {"targetSelectionRange", range(3, 5, 9)}},
                               QJsonObject{{"uri", "https://example.com/x"}, {"range", range(0, 0, 1)}}};
        QCOMPARE(parseLocations(links), (QList<Location>{{"/p/b.h", {{3, 5}, {3, 9}}}}));
        QVERIFY(parseLocations(QJsonValue::Null).isEmpty());
    }

    void parsesWorkspaceEdits()
    {
        const QJsonObject changes{
            {"changes",
             QJsonObject{{uriOf("/p/a.cpp"), QJsonArray{QJsonObject{{"range", range(1, 0, 3)}, {"newText", "bar"}}}}}}};
        QCOMPARE(parseWorkspaceEdit(changes).changes,
                 (QList<DocumentChange>{DocumentEdit{"/p/a.cpp", {}, {{{{1, 0}, {1, 3}}, "bar"}}}}));
    }

    void parsesDocumentChangesInOrderWithTheirFileOperations()
    {
        const QJsonObject edit{
            {"documentChanges",
             QJsonArray{QJsonObject{{"textDocument", QJsonObject{{"uri", uriOf("/p/b.h")}, {"version", 3}}},
                                    {"edits", QJsonArray{QJsonObject{{"range", range(0, 4, 7)}, {"newText", "x"}}}}},
                        QJsonObject{{"kind", "rename"},
                                    {"oldUri", uriOf("/p/b.h")},
                                    {"newUri", uriOf("/p/c.h")},
                                    {"options", QJsonObject{{"overwrite", true}}}},
                        QJsonObject{{"kind", "create"}, {"uri", uriOf("/p/new.h")}},
                        QJsonObject{{"kind", "delete"},
                                    {"uri", uriOf("/p/old")},
                                    {"options", QJsonObject{{"recursive", true}}}}}},
            // Ignored: "documentChanges" wins.
            {"changes", QJsonObject{{uriOf("/p/z.cpp"), QJsonArray{}}}}};

        const WorkspaceEdit parsed = parseWorkspaceEdit(edit);

        using Kind = FileOperation::Kind;
        QCOMPARE(parsed.changes,
                 (QList<DocumentChange>{
                     DocumentEdit{"/p/b.h", 3, {{{{0, 4}, {0, 7}}, "x"}}},
                     FileOperation{.kind = Kind::Rename, .filePath = "/p/b.h", .newPath = "/p/c.h", .overwrite = true},
                     FileOperation{.kind = Kind::Create, .filePath = "/p/new.h"},
                     FileOperation{.kind = Kind::Delete, .filePath = "/p/old", .recursive = true}}));
        QCOMPARE(parsed.filePaths(), (QStringList{"/p/b.h", "/p/c.h", "/p/new.h", "/p/old"}));
        QCOMPARE(parsed.textEditCount(), 1);
    }

    void refusesAnEditForAnotherVersionOfTheText()
    {
        const QTemporaryDir dir;
        const QString path = dir.filePath("a.cpp");
        write(path, "int total;\n");
        EditorTabs tabs(Theme::kodamiko());
        LanguageServers servers;
        servers.setFolder(dir.path());
        auto *editor = EditorTabs::editorIn(tabs.openFile(path));
        QVERIFY(editor);
        // The server saw version 1; the user typed since.
        LspClient *client = servers.clientFor("cpp");
        if (!client)
            QSKIP("no C++ language server");
        servers.syncDocument(client, path, "cpp", editor->utf8Bytes().toByteArray());
        editor->setText("int total = 0;\n");
        const WorkspaceEdit edit{{DocumentEdit{path, 1, {{{{0, 4}, {0, 9}}, "sum"}}}}};

        const WorkspaceEditing::Outcome outcome = WorkspaceEditing::apply(edit, {.tabs = &tabs, .servers = &servers});

        QCOMPARE(outcome.filesChanged, 0);
        QCOMPARE(outcome.errors.size(), 1);
        QCOMPARE(editor->text(), QString("int total = 0;\n"));
    }

    void theUndoKeyUndoesTheWholeRefactoring()
    {
        ClangdProject project;
        EditorWidget *editor = openRenamedInTwoFiles(project);
        QVERIFY(editor);

        QTest::keySequence(editor, QKeySequence::Undo);

        QCOMPARE(editor->text(), QString("int total;\n"));
        QCOMPARE(read(project.dir.filePath("closed.h")), QByteArray("int total;\n"));
    }

    void theRedoKeyRedoesTheWholeRefactoring()
    {
        ClangdProject project;
        EditorWidget *editor = openRenamedInTwoFiles(project);
        QVERIFY(editor);
        QTest::keySequence(editor, QKeySequence::Undo);

        QTest::keySequence(editor, QKeySequence::Redo);

        QCOMPARE(editor->text(), QString("int sum;\n"));
        QCOMPARE(read(project.dir.filePath("closed.h")), QByteArray("int sum;\n"));
    }

    void theUndoKeyDoesNotBringBackPartOfAnUndoneRefactoring()
    {
        ClangdProject project;
        EditorWidget *editor = openRenamedInTwoFiles(project);
        QVERIFY(editor);
        QTest::keySequence(editor, QKeySequence::Undo);

        QTest::keySequence(editor, QKeySequence::Undo);

        QCOMPARE(editor->text(), QString("int total;\n"));
    }

    void appliesEditsFromTheEnd()
    {
        const QString text = "int foo = 1;\r\nfoo += foo;\n";
        const QList<TextEdit> edits{
            {{{0, 4}, {0, 7}}, "bar"},
            {{{1, 0}, {1, 3}}, "bar"},
            {{{1, 7}, {1, 10}}, "bar"},
        };
        QCOMPARE(applyEdits(text, edits), QString("int bar = 1;\r\nbar += bar;\n"));
        // Insertions at one place keep their order; positions past the end
        // of a line or of the text are clamped.
        QCOMPARE(applyEdits("ab", {{{{0, 1}, {0, 1}}, "1"}, {{{0, 1}, {0, 1}}, "2"}, {{{5, 0}, {5, 0}}, "!"}}),
                 QString("a12b!"));
        QCOMPARE(offsetOf("é😀x\nz", {0, 3}), qsizetype(3)); // UTF-16 units
    }

    void readsHoverContentsAsMarkdown()
    {
        QCOMPARE(hoverMarkdown(QJsonObject{{"kind", "markdown"}, {"value", "**f**"}}), QString("**f**"));
        // Plain text keeps its layout: a code block.
        QCOMPARE(hoverMarkdown(QJsonObject{{"kind", "plaintext"}, {"value", "int f()"}}), QString("```\nint f()\n```"));
        QCOMPARE(hoverMarkdown(QJsonArray{"a", QJsonObject{{"language", "cpp"}, {"value", "b"}}}),
                 QString("a\n\n```cpp\nb\n```"));
    }

    void rendersDocumentationInTheEditorTheme()
    {
        const Theme theme = Theme::kodamiko();
        CodeColorizer colorizer;
        const QString html = MarkdownToolTip::html("### `open`\n\nOpens a **file**, see `path`.\n\n```\nint f();\n```",
                                                   {theme, &colorizer, "cpp"});

        QTextDocument shown;
        shown.setHtml(html);
        QCOMPARE(shown.toPlainText(), QString("open\nOpens a file, see path.\nint f();"));
        const auto formatAt = [&shown](const QString &word) {
            QTextCursor cursor(&shown);
            cursor.setPosition(int(shown.toPlainText().indexOf(word)) + 1);
            return cursor.charFormat();
        };
        QCOMPARE(formatAt("open").foreground().color(), theme.accent); // heading
        QCOMPARE(formatAt("file").fontWeight(), int(QFont::Bold));
        QVERIFY(formatAt("path").fontFamilies().toStringList().contains("monospace")); // inline code
        // The fence names no language: the editor's, highlighted as in the editor.
        const QString category = colorizer.categorize("int f();", "cpp").first().category;
        const TextStyle &keyword = theme.syntaxStyles.at(theme.syntaxStyleIndex(category)).second;
        QCOMPARE(formatAt("int").foreground().color(), keyword.foreground);
        // One background: the tooltip's.
        QCOMPARE(formatAt("path").background().style(), Qt::NoBrush);
        QCOMPARE(shown.findBlock(int(shown.toPlainText().indexOf("int"))).blockFormat().background().style(),
                 Qt::NoBrush);
    }

    void findsTheHeaderOfASource()
    {
        const QTemporaryDir dir;
        write(dir.filePath("src/widget.cpp"), "");
        write(dir.filePath("src/widget.h"), "");
        write(dir.filePath("src/other.cpp"), "");
        write(dir.filePath("include/other.hpp"), "");
        QCOMPARE(CodeIntelligence::counterpartOf(dir.filePath("src/widget.cpp")), dir.filePath("src/widget.h"));
        QCOMPARE(CodeIntelligence::counterpartOf(dir.filePath("src/widget.h")), dir.filePath("src/widget.cpp"));
        QCOMPARE(CodeIntelligence::counterpartOf(dir.filePath("src/other.cpp")), dir.filePath("include/other.hpp"));
        QCOMPARE(CodeIntelligence::counterpartOf(dir.filePath("include/other.hpp")), dir.filePath("src/other.cpp"));
        QVERIFY(CodeIntelligence::counterpartOf(dir.filePath("README.md")).isEmpty());
    }

    void convertsPositions()
    {
        EditorWidget editor(Theme::kodamiko());
        editor.setText("é x\nlast");
        QCOMPARE(CodeIntelligence::positionOf(&editor, {0, 2}), 3); // "é" is two bytes
        QCOMPARE(CodeIntelligence::lspPosition(&editor, 3), (Position{0, 2}));
        QCOMPARE(CodeIntelligence::positionOf(&editor, {1, 99}), 9); // clamped to the line
        QCOMPARE(CodeIntelligence::positionOf(&editor, {7, 0}), 9);  // past the end
    }

    // End to end with clangd: definitions across files, usages, rename of
    // an open file and of a file on disk, header and source, documentation.
    void worksWithClangd()
    {
        if (QStandardPaths::findExecutable("clangd").isEmpty())
            QSKIP("clangd is not installed");
        const QTemporaryDir dir;
        const QString root = dir.path();
        write(root + "/compile_flags.txt", "-std=c++20\n");
        write(root + "/total.h", "#pragma once\nint total(int count, int step);\n");
        write(root + "/total.cpp", "#include \"total.h\"\nint total(int count, int step) { return count * step; }\n");
        write(root + "/main.cpp", "#include \"total.h\"\nint main() {\n  return total(2, 3) + total(4, 5);\n}\n");

        EditorTabs tabs(Theme::kodamiko());
        tabs.show();
        SymbolIndex index;
        index.setFolder(root);
        LanguageServers servers;
        servers.setFolder(root);
        CodeNavigator navigator(&tabs, &index);
        ProblemStore problems(&servers);
        CodeIntelligence intelligence({&tabs, &servers, &navigator, &problems});
        connect(&tabs, &EditorTabs::editorOpened, &intelligence, &CodeIntelligence::attach);
        QStringList messages;
        connect(&intelligence, &CodeIntelligence::statusMessage, this,
                [&messages](const QString &message) { messages.append(message); });

        auto *main = EditorTabs::editorIn(tabs.openFile(root + "/main.cpp"));
        QVERIFY(main);
        LspClient *client = servers.clientFor("cpp");
        QVERIFY(client);
        servers.syncDocument(client, main->filePath(), "cpp", main->utf8Bytes().toByteArray());
        // Without waiting for clangd's background index: total.cpp, open,
        // tells where total() is defined.
        servers.syncDocument(client, root + "/total.cpp", "cpp", read(root + "/total.cpp"));
        QTRY_VERIFY_WITH_TIMEOUT(client->isReady(), 30000);
        const auto placeCaret = [](EditorWidget *editor, const QString &before) {
            editor->SendScintilla(
                QsciScintillaBase::SCI_GOTOPOS,
                static_cast<unsigned long>(editor->utf8Bytes().toByteArray().indexOf(before.toUtf8()) + 1));
        };

        // Header and source, by clangd.
        intelligence.switchHeaderSource(); // main.cpp has no header: nothing opens
        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 30000);
        QCOMPARE(tabs.currentFilePath(), root + "/main.cpp");

        // Definition: the function's body, in another file.
        placeCaret(main, "total(2");
        intelligence.goToDefinition();
        QTRY_COMPARE_WITH_TIMEOUT(tabs.currentFilePath(), root + "/total.cpp", 30000);
        EditorWidget *source = tabs.currentEditor();
        QCOMPARE(CodeIntelligence::lspPosition(source, int(source->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS)))
                     .line,
                 1);

        intelligence.switchHeaderSource();
        QTRY_COMPARE_WITH_TIMEOUT(tabs.currentFilePath(), root + "/total.h", 30000);

        // Documentation of the name under the cursor.
        source = EditorTabs::editorIn(tabs.openFile(root + "/total.cpp"));
        placeCaret(source, "total(int");
        intelligence.showDocumentation();
        QTRY_VERIFY_WITH_TIMEOUT(QToolTip::isVisible() && QToolTip::text().contains("total"), 30000);
        QToolTip::hideText();

        // Parameters of the call being typed.
        main = EditorTabs::editorIn(tabs.openFile(root + "/main.cpp"));
        placeCaret(main, "2, 3");
        intelligence.showParameterInfo();
        QTRY_VERIFY_WITH_TIMEOUT(main->SendScintilla(QsciScintillaBase::SCI_CALLTIPACTIVE) != 0, 30000);

        // Rename: the open file in its editor (one undo step), the others on disk.
        tabs.closeFilesUnder(root + "/total.cpp"); // renamed on disk
        placeCaret(main, "total(2");
        const QJsonObject params{
            {"textDocument", QJsonObject{{"uri", uriOf(main->filePath())}}},
            {"position", toJson(CodeIntelligence::lspPosition(
                             main, int(main->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS))))},
            {"newName", "product"}};
        servers.syncDocument(client, main->filePath(), "cpp", main->utf8Bytes().toByteArray());
        servers.syncDocument(client, root + "/total.h", "cpp",
                             tabs.editorFor(root + "/total.h")->utf8Bytes().toByteArray());
        // Once clangd has read the header (a request on it waits for that).
        bool parsed = false;
        client->request("textDocument/documentSymbol",
                        QJsonObject{{"textDocument", QJsonObject{{"uri", uriOf(root + "/total.h")}}}},
                        [&parsed](const QJsonValue &, const QString &) { parsed = true; });
        QTRY_VERIFY_WITH_TIMEOUT(parsed, 30000);
        bool answered = false;
        client->request("textDocument/rename", params, [&](const QJsonValue &result, const QString &error) {
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(intelligence.applyWorkspaceEdit(parseWorkspaceEdit(result.toObject()), "Rename"), 3);
            answered = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(answered, 30000);
        QCOMPARE(main->text(),
                 QString("#include \"total.h\"\nint main() {\n  return product(2, 3) + product(4, 5);\n}\n"));
        QVERIFY(main->isModified());
        main->undo();
        QVERIFY(main->text().contains("total(2, 3) + total(4, 5)")); // one step
        const auto *header = tabs.editorFor(root + "/total.h");
        QVERIFY(header && header->text().contains("int product(int count"));
        QVERIFY(!tabs.editorFor(root + "/total.cpp"));
        QVERIFY(read(root + "/total.cpp").contains("int product(int count"));
    }

    // clangd tells the occurrences of the very symbol: not the homonym in
    // another scope, nor the word in a comment.
    void goesToTheOverrideOfAVirtualFunctionWithClangd()
    {
        if (QStandardPaths::findExecutable("clangd").isEmpty())
            QSKIP("clangd is not installed");
        ClangdProject project;
        EditorWidget *editor = project.open("shapes.cpp", "struct Shape { virtual int area() const = 0; };\n"
                                                          "struct Square : Shape { int area() const override; };\n"
                                                          "int use(const Shape &s) { return s.area(); }\n");
        QVERIFY(editor);
        placeCaretIn(editor, "area(); }");

        project.intelligence.goToImplementation();

        QTRY_COMPARE_WITH_TIMEOUT(caretLine(editor), 1, 30000);
    }

    void goesToTheTypeOfAVariableWithClangd()
    {
        if (QStandardPaths::findExecutable("clangd").isEmpty())
            QSKIP("clangd is not installed");
        ClangdProject project;
        EditorWidget *editor = project.open("point.cpp", "struct Point { int x; };\n"
                                                         "\n"
                                                         "int f() { Point p{}; auto copy = p; return copy.x; }\n");
        QVERIFY(editor);
        placeCaretIn(editor, "copy.x");

        project.intelligence.goToTypeDeclaration();

        QTRY_COMPARE_WITH_TIMEOUT(caretLine(editor), 0, 30000);
    }

    void refusesToRenameAKeywordWithClangd()
    {
        if (QStandardPaths::findExecutable("clangd").isEmpty())
            QSKIP("clangd is not installed");
        ClangdProject project;
        const EditorWidget *editor = project.open("count.cpp", "int count = 0;\n");
        QVERIFY(editor);
        placeCaretIn(editor, "int count");

        project.intelligence.rename();

        QTRY_VERIFY_WITH_TIMEOUT(!project.messages.filter("rename").isEmpty(), 30000);
        QVERIFY(!QApplication::activePopupWidget()); // no name asked
    }

    void checksAgainTheFilesIncludingAHeaderOnceItIsSavedWithClangd()
    {
        if (QStandardPaths::findExecutable("clangd").isEmpty())
            QSKIP("clangd is not installed");
        ClangdProject project;
        write(project.dir.filePath("a.h"), "struct A { int old_; };\n");
        const EditorWidget *source = project.open("a.cpp", "#include \"a.h\"\nint f(A a) { return a.renamed_; }\n");
        QVERIFY(source);
        QTRY_VERIFY_WITH_TIMEOUT(!project.problems.problems(source->filePath()).isEmpty(), 30000);
        auto *header = EditorTabs::editorIn(project.tabs.openFile(project.dir.filePath("a.h")));
        QVERIFY(header);
        header->setText("struct A { int renamed_; };\n");

        QVERIFY(project.tabs.saveCurrent());

        QTRY_VERIFY_WITH_TIMEOUT(project.problems.problems(source->filePath()).isEmpty(), 30000);
    }

    void renamesThroughThePreviewThenUndoesWithClangd()
    {
        if (QStandardPaths::findExecutable("clangd").isEmpty())
            QSKIP("clangd is not installed");
        ClangdProject project;
        const EditorWidget *editor = project.open("count.cpp", "int count = 0;\nint next() { return ++count; }\n");
        QVERIFY(editor);
        placeCaretIn(editor, "count = 0");

        project.intelligence.rename();
        QTRY_VERIFY_WITH_TIMEOUT(project.tabs.window()->findChild<RenameSymbolPopup *>(), 30000);
        auto *rename = project.tabs.window()->findChild<RenameSymbolPopup *>();
        rename->field()->setText("total");
        rename->preview();
        QTRY_VERIFY_WITH_TIMEOUT(project.tabs.window()->findChild<RefactoringPreviewPopup *>(), 30000);
        auto *preview = project.tabs.window()->findChild<RefactoringPreviewPopup *>();
        QCOMPARE(editor->text(), QString("int count = 0;\nint next() { return ++count; }\n")); // not yet
        preview->accept();

        QTRY_COMPARE(editor->text(), QString("int total = 0;\nint next() { return ++total; }\n"));
        QTRY_COMPARE(project.intelligence.history()->lastDescription(), QString("Rename “count” to “total”"));
        project.intelligence.undoRefactoring();
        QCOMPARE(editor->text(), QString("int count = 0;\nint next() { return ++count; }\n"));
    }

    void showsTheCommentsARenameChangesBeforeMakingItWithClangd()
    {
        if (QStandardPaths::findExecutable("clangd").isEmpty())
            QSKIP("clangd is not installed");
        ClangdProject project;
        const EditorWidget *editor = project.open("count.cpp", "// count of the calls\nint count = 0;\n");
        QVERIFY(editor);
        placeCaretIn(editor, "count = 0");

        project.intelligence.rename();
        QTRY_VERIFY_WITH_TIMEOUT(project.tabs.window()->findChild<RenameSymbolPopup *>(), 30000);
        const auto *rename = project.tabs.window()->findChild<RenameSymbolPopup *>();
        rename->field()->setText("total");
        rename->commentsAndStrings()->setChecked(true);
        QTest::keyClick(rename->field(), Qt::Key_Return); // Refactor

        QTRY_VERIFY_WITH_TIMEOUT(project.tabs.window()->findChild<RefactoringPreviewPopup *>(), 30000);
        auto *preview = project.tabs.window()->findChild<RefactoringPreviewPopup *>();
        QCOMPARE(preview->files()->count(), 2); // the code, then the comment
        preview->accept();
        QTRY_COMPARE(editor->text(), QString("// total of the calls\nint total = 0;\n"));
    }

    void findsTheUsagesOfANameByKindWithClangd()
    {
        if (QStandardPaths::findExecutable("clangd").isEmpty())
            QSKIP("clangd is not installed");
        ClangdProject project;
        const EditorWidget *editor =
            project.open("count.cpp", "int count = 0; // count\nint next() { count = 2; return count; }\n");
        QVERIFY(editor);
        placeCaretIn(editor, "count = 0");
        const QSignalSpy found(&project.intelligence, &CodeIntelligence::usagesFound);

        project.intelligence.findUsages();

        QTRY_COMPARE_WITH_TIMEOUT(found.size(), 1, 30000);
        const auto results = found[0][0].value<UsageResults>();
        QVERIFY(results.isSemantic);
        QList<UsageKind> kinds;
        for (const Usage &usage : results.usages)
            kinds.append(usage.kind);
        QCOMPARE(kinds, (QList<UsageKind>{UsageKind::Declaration, UsageKind::Write, UsageKind::Read}));
    }

    void showsTheCallersAndSubtypesWithClangd()
    {
        if (QStandardPaths::findExecutable("clangd").isEmpty())
            QSKIP("clangd is not installed");
        ClangdProject project;
        const EditorWidget *editor = project.open("calls.cpp", "struct Shape { virtual ~Shape() = default; };\n"
                                                               "struct Square : Shape {};\n"
                                                               "int total() { return 1; }\n"
                                                               "int sum() { return total() + total(); }\n");
        QVERIFY(editor);
        QSignalSpy found(&project.intelligence, &CodeIntelligence::hierarchyFound);
        placeCaretIn(editor, "total() { return 1");

        project.intelligence.showCallHierarchy();
        QTRY_COMPARE_WITH_TIMEOUT(found.size(), 1, 30000);
        auto calls = found[0][0].value<Hierarchy>();
        QList<LspProtocol::HierarchyItem> callers;
        calls.children(Hierarchy::Direction::Up, calls.roots.first(),
                       [&callers](const QList<LspProtocol::HierarchyItem> &items) { callers = items; });
        QTRY_COMPARE_WITH_TIMEOUT(callers.size(), 1, 30000);
        QCOMPARE(callers[0].name, QString("sum"));

        placeCaretIn(editor, "Shape {");
        project.intelligence.showTypeHierarchy();
        QTRY_COMPARE_WITH_TIMEOUT(found.size(), 2, 30000);
        auto typeHierarchy = found[1][0].value<Hierarchy>();
        QList<LspProtocol::HierarchyItem> subtypes;
        typeHierarchy.children(Hierarchy::Direction::Down, typeHierarchy.roots.first(),
                               [&subtypes](const QList<LspProtocol::HierarchyItem> &items) { subtypes = items; });
        QTRY_COMPARE_WITH_TIMEOUT(subtypes.size(), 1, 30000);
        QCOMPARE(subtypes[0].name, QString("Square"));
    }

    void updatesTheIncludesOfAMovedFileAsOneRefactoring()
    {
        ClangdProject project;
        write(project.dir.filePath("main.cpp"), "#include \"total.h\"\n");
        write(project.dir.filePath("total.h"), "#pragma once\n");
        const QSignalSpy indexed(&project.occurrences, &OccurrenceIndex::indexingFinished);
        project.occurrences.setFolder(project.dir.path());
        QTRY_VERIFY(!indexed.isEmpty());
        QDir().mkpath(project.dir.filePath("include"));
        QVERIFY(QFile::rename(project.dir.filePath("total.h"), project.dir.filePath("include/total.h")));

        project.intelligence.updateReferencesToMovedFile(project.dir.filePath("total.h"),
                                                         project.dir.filePath("include/total.h"));

        QTRY_COMPARE(read(project.dir.filePath("main.cpp")), QByteArray("#include \"include/total.h\"\n"));
        QCOMPARE(project.intelligence.history()->lastDescription(), QString("Update the references to “total.h”"));
        project.intelligence.undoRefactoring();
        QCOMPARE(read(project.dir.filePath("main.cpp")), QByteArray("#include \"total.h\"\n"));
    }

    void movesAClassToItsOwnFileAfterAPreview()
    {
        ClangdProject project;
        const QString header = project.dir.filePath("shapes.h");
        write(header, "#pragma once\n\nclass Square {};\n\nclass Circle {};\n");
        const auto *editor = EditorTabs::editorIn(project.tabs.openFile(header));
        QVERIFY(editor);
        placeCaretIn(editor, "Circle {}");

        project.intelligence.moveClassToOwnFile();
        auto *preview = project.tabs.window()->findChild<RefactoringPreviewPopup *>();
        QVERIFY(preview);
        preview->accept();

        QTRY_COMPARE(read(project.dir.filePath("Circle.h")), QByteArray("#pragma once\n\nclass Circle {};\n"));
        QCOMPARE(editor->text(), QString("#pragma once\n#include \"Circle.h\"\n\nclass Square {};\n\n"));
        QCOMPARE(project.tabs.currentFilePath(), project.dir.filePath("Circle.h")); // opened
        project.intelligence.undoRefactoring();
        QVERIFY2(!QFileInfo::exists(project.dir.filePath("Circle.h")), qPrintable(project.messages.join('\n')));
        QCOMPARE(editor->text(), QString("#pragma once\n\nclass Square {};\n\nclass Circle {};\n"));
    }

    void renamesAFileWithItsIncludesAsOneRefactoring()
    {
        ClangdProject project;
        write(project.dir.filePath("main.cpp"), "#include \"total.h\"\n");
        write(project.dir.filePath("total.h"), "#pragma once\n");
        indexProject(project);
        std::optional<RefactoringOutcome> outcome;

        project.intelligence.renameFile(project.dir.filePath("total.h"), project.dir.filePath("inc/sum.h"),
                                        Review::WhenNeeded,
                                        [&outcome](const RefactoringOutcome &result) { outcome = result; });

        QTRY_VERIFY(outcome);
        QCOMPARE(outcome->status, RefactoringOutcome::Status::Applied);
        QCOMPARE(read(project.dir.filePath("main.cpp")), QByteArray("#include \"inc/sum.h\"\n"));
        QVERIFY(QFileInfo::exists(project.dir.filePath("inc/sum.h")));
        QCOMPARE(project.intelligence.history()->lastDescription(),
                 QDir::toNativeSeparators("Move “total.h” to inc/sum.h"));
        project.intelligence.undoRefactoring(); // the move and the include, together
        QVERIFY(QFileInfo::exists(project.dir.filePath("total.h")));
        QCOMPARE(read(project.dir.filePath("main.cpp")), QByteArray("#include \"total.h\"\n"));
    }

    void reportsARefactoringCancelledInItsPreview()
    {
        ClangdProject project;
        write(project.dir.filePath("a.h"), "#pragma once\n");
        std::optional<RefactoringOutcome> outcome;

        project.intelligence.renameFile(project.dir.filePath("a.h"), project.dir.filePath("b.h"), Review::Always,
                                        [&outcome](const RefactoringOutcome &result) { outcome = result; });
        QTRY_VERIFY(project.tabs.window()->findChild<RefactoringPreviewPopup *>());
        project.tabs.window()->findChild<RefactoringPreviewPopup *>()->dismiss();

        QTRY_VERIFY(outcome);
        QCOMPARE(outcome->status, RefactoringOutcome::Status::Cancelled);
        QVERIFY(QFileInfo::exists(project.dir.filePath("a.h")));
    }

    void highlightsTheSymbolsOccurrencesWithClangd()
    {
        if (QStandardPaths::findExecutable("clangd").isEmpty())
            QSKIP("clangd is not installed");
        const QTemporaryDir dir;
        const QString root = dir.path();
        write(root + "/compile_flags.txt", "-std=c++20\n");
        write(root + "/count.cpp", "int count = 0; // count\n"
                                   "int next() { return ++count; }\n"
                                   "int other() { int count = 2; return count; }\n");
        EditorTabs tabs(Theme::kodamiko());
        tabs.show();
        SymbolIndex index;
        index.setFolder(root);
        LanguageServers servers;
        servers.setFolder(root);
        CodeNavigator navigator(&tabs, &index);
        ProblemStore problems(&servers);
        const CodeIntelligence intelligence({&tabs, &servers, &navigator, &problems});
        connect(&tabs, &EditorTabs::editorOpened, &intelligence, &CodeIntelligence::attach);
        auto *editor = EditorTabs::editorIn(tabs.openFile(root + "/count.cpp"));
        QVERIFY(editor);
        LspClient *client = servers.clientFor("cpp");
        QVERIFY(client);
        servers.syncDocument(client, editor->filePath(), "cpp", editor->utf8Bytes().toByteArray());
        QTRY_VERIFY_WITH_TIMEOUT(client->isReady(), 30000);

        editor->setCursorPosition(0, 5);
        editor->symbolOccurrences()->refreshNow();

        QTRY_COMPARE_WITH_TIMEOUT(editor->symbolOccurrences()->occurrences().size(), 2, 30000);
        const QByteArray text = editor->utf8Bytes().toByteArray();
        QCOMPARE(editor->symbolOccurrences()->occurrences().last().start, int(text.indexOf("count; }")));
    }
};

QTEST_MAIN(TestCodeIntelligence)
#include "tst_codeintelligence.moc"
