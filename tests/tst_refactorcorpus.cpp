// SPDX-License-Identifier: GPL-3.0-only

// The refactoring corpus (tests/refactoring): each case is a folder with
// the project before the refactoring (before/, "§" marking the cursor),
// the project expected after it (after/), and case.json saying what to
// do. Each one is run, its result compared file by file with after/, then
// undone and compared with before/.
//
// - move/: files moved; the includes and imports follow (FileReferences).
//   case.json: {"moves": [["from", "to"], …]}
// - moveClass/: Move Class to Its Own File at the cursor.
//   case.json: {"source": "the header's source, if any"}
// - textual/: a rename's occurrences in comments, strings and non-code
//   files (RenameAnalysis without a server's edit).
//   case.json: {"name": "…", "newName": "…"}
// - rename/: a rename through the language server, the whole way
//   (popup, server, comments and strings, preview, Undo Refactoring);
//   skipped when the language's server is not installed in Duet's
//   toolchains.
//   case.json: {"newName": "…", "commentsAndStrings": true}
// - usages/: Find Usages through the language server at the cursor; the
//   case's usages.txt lists them, one "file:line:column kind" a line (from
//   1), as the Usages pane has them; skipped as rename/ is.

#include "diagnostics/ProblemStore.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "index/OccurrenceIndex.h"
#include "lsp/LanguageServers.h"
#include "lsp/LspClient.h"
#include "navigation/CodeIntelligence.h"
#include "navigation/CodeNavigator.h"
#include "navigation/SymbolIndex.h"
#include "navigation/WorkspaceEditing.h"
#include "projects/ProjectConfig.h"
#include "refactor/FileReferences.h"
#include "refactor/MoveClass.h"
#include "refactor/RefactoringHistory.h"
#include "refactor/RefactoringPreviewPopup.h"
#include "refactor/RenameAnalysis.h"
#include "refactor/RenameSymbolPopup.h"
#include "syntax/LanguageRegistry.h"
#include "toolchains/Toolchains.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMap>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVersionNumber>
#include <QtTest>

#include <algorithm>
#include <optional>
#include <ranges>

using namespace LspProtocol;

namespace {

// Files by path relative to their tree's root.
using Tree = QMap<QString, QByteArray>;

constexpr const char *cursorMarker = "§";
constexpr int serverTimeoutMs = 60000;

// A case's project, copied without its cursor marker.
struct Copied {
    QString cursorFile; // relative; empty when no file holds "§"
    Position cursor;
};

QString corpusFolder()
{
    return QStringLiteral(DUET_REFACTORING_CORPUS);
}

Tree readTree(const QString &root)
{
    Tree tree;
    QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            tree.insert(QDir(root).relativeFilePath(path), file.readAll());
    }
    return tree;
}

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

Position positionOfMarker(const QByteArray &content)
{
    const qsizetype at = content.indexOf(cursorMarker);
    const qsizetype lineStart = content.lastIndexOf('\n', at - 1) + 1;
    return {int(content.left(at).count('\n')), int(QString::fromUtf8(content.mid(lineStart, at - lineStart)).size())};
}

// Copies `tree` to `root`, the cursor marker taken out.
Copied copyTree(const Tree &tree, const QString &root)
{
    Copied copied;
    for (auto it = tree.cbegin(); it != tree.cend(); ++it) {
        QByteArray content = it.value();
        if (content.contains(cursorMarker)) {
            copied.cursorFile = it.key();
            copied.cursor = positionOfMarker(content);
            content.replace(cursorMarker, "");
        }
        writeFile(root + QLatin1Char('/') + it.key(), content);
    }
    return copied;
}

Tree withoutMarker(Tree tree)
{
    for (QByteArray &content : tree)
        content.replace(cursorMarker, "");
    return tree;
}

// The files as the user sees them: an open file's editor text, unsaved
// changes included, else the file on disk.
Tree currentTree(const QString &root, const EditorTabs &tabs)
{
    Tree tree = readTree(root);
    for (auto it = tree.begin(); it != tree.end(); ++it) {
        if (const EditorWidget *editor = tabs.editorFor(root + QLatin1Char('/') + it.key()))
            it.value() = editor->text().toUtf8();
    }
    return tree;
}

// Compares two trees, the first difference in the message.
bool sameTrees(const Tree &actual, const Tree &expected, QString &difference)
{
    const QStringList actualFiles = actual.keys();
    const QStringList expectedFiles = expected.keys();
    if (actualFiles != expectedFiles) {
        difference = QStringLiteral("files: %1\nexpected: %2").arg(actualFiles.join(", "), expectedFiles.join(", "));
        return false;
    }
    for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
        if (actual.value(it.key()) != it.value()) {
            difference = QStringLiteral("%1 is\n%2\n--- expected\n%3")
                             .arg(it.key(), QString::fromUtf8(actual.value(it.key())), QString::fromUtf8(it.value()));
            return false;
        }
    }
    return true;
}

QJsonObject caseOf(const QString &folder)
{
    QFile file(folder + QStringLiteral("/case.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// One row per case of `operation`.
void addCases(const QString &operation)
{
    QTest::addColumn<QString>("folder");
    const QDir cases(corpusFolder() + QLatin1Char('/') + operation);
    const QStringList names = cases.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names)
        QTest::newRow(qPrintable(name)) << cases.filePath(name);
}

void indexFolder(OccurrenceIndex &index, const QString &root)
{
    const QSignalSpy finished(&index, &OccurrenceIndex::indexingFinished);
    index.setFolder(root);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 30000);
}

// Where the user's Duet downloaded its language servers.
QString userToolchains()
{
    return QDir::homePath() + QStringLiteral("/.local/share/Duet/Duet/toolchains");
}

// The latest complete download of `tool`, empty if none.
QString latestVersion(const Toolchains &toolchains, const QString &tool)
{
    QStringList versions =
        QDir(userToolchains() + QLatin1Char('/') + tool).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    std::ranges::sort(versions, [](const QString &a, const QString &b) {
        return QVersionNumber::fromString(a) < QVersionNumber::fromString(b);
    });
    for (const QString &version : std::views::reverse(versions)) {
        if (toolchains.isInstalled(tool, version))
            return version;
    }
    return {};
}

// The project setup that runs `languageId`'s server, if it is installed.
std::optional<LanguageSetup> serverSetup(const QString &languageId, const Toolchains &toolchains)
{
    const QString family = ProjectLanguages::familyOf(languageId);
    if (family == QLatin1String("cpp")) {
        if (QStandardPaths::findExecutable(QStringLiteral("clangd")).isEmpty())
            return std::nullopt;
        return LanguageSetup{
            family, true, QStringLiteral("c++20"), {}, QStringLiteral("clangd"), QStringLiteral("system")};
    }
    const QString server =
        family == QLatin1String("python") ? QStringLiteral("pyright") : QStringLiteral("typescript-language-server");
    const QString version = latestVersion(toolchains, server);
    if (version.isEmpty())
        return std::nullopt;
    return LanguageSetup{family, true, {}, {}, server, version};
}

// A project of the corpus open in Duet's code intelligence, with its
// language server.
struct ServerProject {
    QTemporaryDir dir;
    Toolchains toolchains{userToolchains(), true};
    EditorTabs tabs{Theme::dark()};
    SymbolIndex symbols;
    LanguageServers servers;
    CodeNavigator navigator{&tabs, &symbols};
    ProblemStore problems{&servers};
    OccurrenceIndex occurrences;
    CodeIntelligence intelligence{{&tabs, &servers, &navigator, &problems, &occurrences.store()}};

    ServerProject() { tabs.show(); }

    ~ServerProject() { QFile::remove(OccurrenceIndex::cachePathFor(dir.path())); }

    ServerProject(const ServerProject &) = delete;
    ServerProject &operator=(const ServerProject &) = delete;

    QString path(const QString &relative) const { return dir.filePath(relative); }
};

// Opens every file of `languageId`'s family in its server and waits until
// the server has read each: a rename then reaches them all, without
// waiting for a background index.
bool openInServer(ServerProject &project, LspClient *client, const QString &languageId)
{
    const QString family = ProjectLanguages::familyOf(languageId);
    const Tree tree = readTree(project.dir.path());
    for (auto it = tree.cbegin(); it != tree.cend(); ++it) {
        const QString path = project.path(it.key());
        const QString language = LanguageRegistry::languageForFile(path);
        if (ProjectLanguages::familyOf(language) != family)
            continue;
        project.servers.syncDocument(client, path, language, it.value());
        bool read = false;
        client->request(QStringLiteral("textDocument/documentSymbol"),
                        QJsonObject{{"textDocument", QJsonObject{{"uri", uriOf(path)}}}},
                        [&read](const QJsonValue &, const QString &) { read = true; });
        if (!QTest::qWaitFor([&read] { return read; }, serverTimeoutMs))
            return false;
    }
    return true;
}

// Waits until the servers have reported no task (indexing, analysis) for
// a moment: their answers then cover the whole project.
bool waitUntilIdle(const LanguageServers &servers)
{
    constexpr int quietMs = 500;
    QElapsedTimer quiet;
    quiet.start();
    return QTest::qWaitFor(
        [&servers, &quiet] {
            if (!servers.tasks().isEmpty())
                quiet.restart();
            return quiet.elapsed() >= quietMs;
        },
        serverTimeoutMs);
}

QString kindName(UsageKind kind)
{
    switch (kind) {
    case UsageKind::Declaration:
        return QStringLiteral("declaration");
    case UsageKind::Read:
        return QStringLiteral("read");
    case UsageKind::Write:
        return QStringLiteral("write");
    case UsageKind::Call:
        return QStringLiteral("call");
    case UsageKind::Import:
        return QStringLiteral("import");
    case UsageKind::Textual:
        return QStringLiteral("text");
    }
    return {};
}

// Copies `folder`'s before/ into `project`, starts the server of the
// cursor's file, and opens that file with the cursor placed. nullptr when
// the server is not installed (the test then skipped) or a check failed.
EditorWidget *openWithServer(ServerProject &project, const QString &folder)
{
    const Copied copied = copyTree(readTree(folder + QStringLiteral("/before")), project.dir.path());
    if (copied.cursorFile.isEmpty()) {
        QTest::qFail("no § in before/", __FILE__, __LINE__);
        return nullptr;
    }
    const QString path = project.path(copied.cursorFile);
    const QString language = LanguageRegistry::languageForFile(path);
    const std::optional<LanguageSetup> setup = serverSetup(language, project.toolchains);
    if (!setup) {
        QTest::qSkip(qPrintable(QStringLiteral("No language server for %1").arg(language)), __FILE__, __LINE__);
        return nullptr;
    }
    ProjectConfig config;
    config.path = project.dir.path();
    config.languages.append(*setup);
    project.servers.setFolder(project.dir.path());
    project.servers.setProject(config, &project.toolchains);
    indexFolder(project.occurrences, project.dir.path());
    auto *editor = EditorTabs::editorIn(project.tabs.openFile(path));
    LspClient *client = project.servers.clientFor(language);
    const bool isReady = editor && client && QTest::qWaitFor([client] { return client->isReady(); }, serverTimeoutMs);
    if (!isReady || !openInServer(project, client, language) || !waitUntilIdle(project.servers)) {
        QTest::qFail(
            qPrintable(QStringLiteral("%1 did not start: %2").arg(language, project.servers.failure(language))),
            __FILE__, __LINE__);
        return nullptr;
    }
    editor->SendScintilla(QsciScintillaBase::SCI_GOTOPOS,
                          static_cast<unsigned long>(CodeIntelligence::positionOf(editor, copied.cursor)));
    return editor;
}

} // namespace

class TestRefactorCorpus : public QObject {
    Q_OBJECT

private slots:

    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void cleanupTestCase() { QDir(QFileInfo(OccurrenceIndex::cachePathFor("/")).absolutePath()).removeRecursively(); }

    void moveUpdatesTheReferences_data() { addCases(QStringLiteral("move")); }

    void moveUpdatesTheReferences()
    {
        QFETCH(const QString, folder);
        const Tree before = readTree(folder + QStringLiteral("/before"));
        const QTemporaryDir dir;
        copyTree(before, dir.path());
        OccurrenceIndex index;
        indexFolder(index, dir.path());
        QList<FileReferences::Move> moves;
        for (const QJsonValue &move : caseOf(folder).value("moves").toArray()) {
            const QString from = dir.filePath(move[0].toString());
            const QString to = dir.filePath(move[1].toString());
            QDir().mkpath(QFileInfo(to).absolutePath());
            QVERIFY2(QDir().rename(from, to), qPrintable(from));
            if (QFileInfo(to).isDir())
                moves.append(FileReferences::filesOf(from, to));
            else
                moves.append({from, to});
        }
        const Tree moved = readTree(dir.path());
        EditorTabs tabs(Theme::dark());
        LanguageServers servers;

        const WorkspaceEdit edit = FileReferences::update({moves, dir.path(), &index.store(), {}});
        const WorkspaceEditing::Outcome outcome = WorkspaceEditing::apply(edit, {&tabs, &servers});

        QVERIFY2(outcome.errors.isEmpty(), qPrintable(outcome.errors.join('\n')));
        QString difference;
        QVERIFY2(sameTrees(readTree(dir.path()), readTree(folder + QStringLiteral("/after")), difference),
                 qPrintable(difference));
        QVERIFY(outcome.journal.revert({&tabs, &servers}).isEmpty());
        QVERIFY2(sameTrees(readTree(dir.path()), moved, difference), qPrintable(difference));
    }

    void moveClassSplitsTheFile_data() { addCases(QStringLiteral("moveClass")); }

    void moveClassSplitsTheFile()
    {
        QFETCH(const QString, folder);
        const Tree before = readTree(folder + QStringLiteral("/before"));
        const QTemporaryDir dir;
        const Copied copied = copyTree(before, dir.path());
        QVERIFY2(!copied.cursorFile.isEmpty(), "no § in before/");
        const QString path = dir.filePath(copied.cursorFile);
        const QString language = LanguageRegistry::languageForFile(path);
        const QString source = caseOf(folder).value("source").toString();
        const Tree unmarked = withoutMarker(before);
        EditorTabs tabs(Theme::dark());
        LanguageServers servers;

        const MoveClass::Result result =
            MoveClass::plan({.path = path,
                             .languageId = language == QLatin1String("java") ? language : QStringLiteral("cpp"),
                             .text = QString::fromUtf8(unmarked.value(copied.cursorFile)),
                             .cursor = copied.cursor,
                             .sourcePath = source.isEmpty() ? QString() : dir.filePath(source),
                             .sourceText = QString::fromUtf8(unmarked.value(source))});
        const QString expectedError = caseOf(folder).value("error").toString();
        if (!expectedError.isEmpty()) {
            QCOMPARE(result.error, expectedError);
            return;
        }
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        const WorkspaceEditing::Outcome outcome = WorkspaceEditing::apply(result.edit, {&tabs, &servers});

        QVERIFY2(outcome.errors.isEmpty(), qPrintable(outcome.errors.join('\n')));
        QString difference;
        QVERIFY2(sameTrees(readTree(dir.path()), readTree(folder + QStringLiteral("/after")), difference),
                 qPrintable(difference));
        QVERIFY(outcome.journal.revert({&tabs, &servers}).isEmpty());
        QVERIFY2(sameTrees(readTree(dir.path()), unmarked, difference), qPrintable(difference));
    }

    void renameReachesCommentsAndStrings_data() { addCases(QStringLiteral("textual")); }

    void renameReachesCommentsAndStrings()
    {
        QFETCH(const QString, folder);
        const Tree before = readTree(folder + QStringLiteral("/before"));
        const QTemporaryDir dir;
        copyTree(before, dir.path());
        OccurrenceIndex index;
        indexFolder(index, dir.path());
        const QJsonObject description = caseOf(folder);
        EditorTabs tabs(Theme::dark());
        LanguageServers servers;

        const RenameAnalysis::Result analysis = RenameAnalysis::analyze({
            .name = description.value("name").toString(),
            .newName = description.value("newName").toString(),
            .codeEdit = {},
            .index = &index.store(),
            .unsavedTexts = {},
            .projectFolder = dir.path(),
            .searchesCommentsAndStrings = true,
        });
        const WorkspaceEditing::Outcome outcome = WorkspaceEditing::apply(analysis.textualEdit, {&tabs, &servers});

        QVERIFY2(outcome.errors.isEmpty(), qPrintable(outcome.errors.join('\n')));
        QString difference;
        QVERIFY2(sameTrees(readTree(dir.path()), readTree(folder + QStringLiteral("/after")), difference),
                 qPrintable(difference));
        QVERIFY(outcome.journal.revert({&tabs, &servers}).isEmpty());
        QVERIFY2(sameTrees(readTree(dir.path()), before, difference), qPrintable(difference));
    }

    void findUsagesKnowsEachKind_data() { addCases(QStringLiteral("usages")); }

    void findUsagesKnowsEachKind()
    {
        QFETCH(const QString, folder);
        ServerProject project;
        const EditorWidget *editor = openWithServer(project, folder);
        if (!editor)
            return; // skipped or failed
        const QSignalSpy found(&project.intelligence, &CodeIntelligence::usagesFound);

        project.intelligence.findUsages();

        QTRY_COMPARE_WITH_TIMEOUT(found.size(), 1, serverTimeoutMs);
        const auto results = found[0][0].value<UsageResults>();
        QVERIFY(results.isSemantic);
        QStringList lines;
        for (const Usage &usage : results.usages) {
            lines.append(QStringLiteral("%1:%2:%3 %4")
                             .arg(QDir(project.dir.path()).relativeFilePath(usage.path))
                             .arg(usage.line + 1)
                             .arg(usage.column + 1)
                             .arg(kindName(usage.kind)));
        }
        QFile expected(folder + QStringLiteral("/usages.txt"));
        QVERIFY(expected.open(QIODevice::ReadOnly));
        QCOMPARE(lines.join(QLatin1Char('\n')), QString::fromUtf8(expected.readAll()).trimmed());
    }

    void renameGoesThroughTheServer_data() { addCases(QStringLiteral("rename")); }

    void renameGoesThroughTheServer()
    {
        QFETCH(const QString, folder);
        const Tree before = readTree(folder + QStringLiteral("/before"));
        const QJsonObject description = caseOf(folder);
        ServerProject project;
        if (!openWithServer(project, folder))
            return; // skipped or failed

        project.intelligence.rename();
        QTRY_VERIFY_WITH_TIMEOUT(project.tabs.window()->findChild<RenameSymbolPopup *>(), serverTimeoutMs);
        const auto *popup = project.tabs.window()->findChild<RenameSymbolPopup *>();
        popup->field()->setText(description.value("newName").toString());
        popup->commentsAndStrings()->setChecked(description.value("commentsAndStrings").toBool(true));
        QTest::keyClick(popup->field(), Qt::Key_Return);
        // The preview opens for the comments and strings, or a conflict.
        QVERIFY(QTest::qWaitFor(
            [&project] {
                if (auto *preview = project.tabs.window()->findChild<RefactoringPreviewPopup *>();
                    preview && preview->isVisible())
                    preview->accept();
                return !project.intelligence.history()->lastDescription().isEmpty();
            },
            serverTimeoutMs));

        QString difference;
        QVERIFY2(sameTrees(currentTree(project.dir.path(), project.tabs), readTree(folder + QStringLiteral("/after")),
                           difference),
                 qPrintable(difference));
        project.intelligence.undoRefactoring();
        QVERIFY2(sameTrees(currentTree(project.dir.path(), project.tabs), withoutMarker(before), difference),
                 qPrintable(difference));
    }
};

QTEST_MAIN(TestRefactorCorpus)
#include "tst_refactorcorpus.moc"
