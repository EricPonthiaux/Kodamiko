#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "lsp/LanguageServers.h"
#include "lsp/LspClient.h"
#include "lsp/LspProtocol.h"
#include "navigation/CodeIntelligence.h"
#include "navigation/CodeNavigator.h"
#include "navigation/SymbolIndex.h"
#include "ui/Theme.h"

#include <QJsonArray>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
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
            {"changes", QJsonObject{{uriOf("/p/a.cpp"), QJsonArray{QJsonObject{{"range", range(1, 0, 3)}, {"newText", "bar"}}}}}}};
        QCOMPARE(parseWorkspaceEdit(changes).value("/p/a.cpp"), (QList<TextEdit>{{{{1, 0}, {1, 3}}, "bar"}}));

        const QJsonObject documentChanges{
            {"documentChanges",
             QJsonArray{QJsonObject{{"textDocument", QJsonObject{{"uri", uriOf("/p/b.h")}, {"version", 3}}},
                                    {"edits", QJsonArray{QJsonObject{{"range", range(0, 4, 7)}, {"newText", "x"}}}}},
                        QJsonObject{{"kind", "create"}, {"uri", uriOf("/p/new.h")}}}}};
        const WorkspaceEdit edit = parseWorkspaceEdit(documentChanges);
        QCOMPARE(edit.keys(), QStringList{"/p/b.h"});
        QCOMPARE(edit.value("/p/b.h").first().newText, QString("x"));
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

    void readsHoverContents()
    {
        QCOMPARE(plainText(QJsonObject{{"kind", "plaintext"}, {"value", "int f()"}}), QString("int f()"));
        QCOMPARE(plainText(QJsonArray{"a", QJsonObject{{"language", "cpp"}, {"value", "b"}}}), QString("a\n\nb"));
    }

    void findsTheHeaderOfASource()
    {
        QTemporaryDir dir;
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
        EditorWidget editor(Theme::dark());
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
        QTemporaryDir dir;
        const QString root = dir.path();
        write(root + "/compile_flags.txt", "-std=c++20\n");
        write(root + "/total.h", "#pragma once\nint total(int count, int step);\n");
        write(root + "/total.cpp", "#include \"total.h\"\nint total(int count, int step) { return count * step; }\n");
        write(root + "/main.cpp", "#include \"total.h\"\nint main() {\n  return total(2, 3) + total(4, 5);\n}\n");

        EditorTabs tabs(Theme::dark());
        tabs.show();
        SymbolIndex index;
        index.setFolder(root);
        LanguageServers servers;
        servers.setFolder(root);
        CodeNavigator navigator(&tabs, &index);
        CodeIntelligence intelligence(&tabs, &servers, &navigator);
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
            editor->SendScintilla(QsciScintillaBase::SCI_GOTOPOS,
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
        QCOMPARE(CodeIntelligence::lspPosition(source, int(source->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS))).line, 1);

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
        QJsonObject params{{"textDocument", QJsonObject{{"uri", uriOf(main->filePath())}}},
                           {"position", toJson(CodeIntelligence::lspPosition(
                                            main, int(main->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS))))},
                           {"newName", "product"}};
        servers.syncDocument(client, main->filePath(), "cpp", main->utf8Bytes().toByteArray());
        servers.syncDocument(client, root + "/total.h", "cpp", tabs.editorFor(root + "/total.h")->utf8Bytes().toByteArray());
        // Once clangd has read the header (a request on it waits for that).
        bool parsed = false;
        client->request("textDocument/documentSymbol",
                        QJsonObject{{"textDocument", QJsonObject{{"uri", uriOf(root + "/total.h")}}}},
                        [&parsed](const QJsonValue &, const QString &) { parsed = true; });
        QTRY_VERIFY_WITH_TIMEOUT(parsed, 30000);
        bool answered = false;
        client->request("textDocument/rename", params, [&](const QJsonValue &result, const QString &error) {
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(intelligence.applyWorkspaceEdit(parseWorkspaceEdit(result.toObject())), 3);
            answered = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(answered, 30000);
        QCOMPARE(main->text(), QString("#include \"total.h\"\nint main() {\n  return product(2, 3) + product(4, 5);\n}\n"));
        QVERIFY(main->isModified());
        main->undo();
        QVERIFY(main->text().contains("total(2, 3) + total(4, 5)")); // one step
        auto *header = tabs.editorFor(root + "/total.h");
        QVERIFY(header && header->text().contains("int product(int count"));
        QVERIFY(!tabs.editorFor(root + "/total.cpp"));
        QVERIFY(read(root + "/total.cpp").contains("int product(int count"));
    }
};

QTEST_MAIN(TestCodeIntelligence)
#include "tst_codeintelligence.moc"
