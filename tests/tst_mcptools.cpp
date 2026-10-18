#include "claude/mcp/DuetTools.h"
#include "claude/mcp/McpRegistry.h"
#include "claude/mcp/McpSession.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "git/GitProcess.h"
#include "git/RepositoryStatus.h"
#include "navigation/CodeNavigator.h"
#include "navigation/SymbolIndex.h"
#include "terminal/ProcessSpec.h"
#include "terminal/TerminalWidget.h"
#ifdef DUET_WITH_DIAGRAMS
#include "viewers/DiagramView.h"
#include "viewers/diagram/Board.h"
#endif

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest>

namespace {

void write(const QTemporaryDir &dir, const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(dir.filePath(path)).absolutePath());
    QFile file(dir.filePath(path));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

// The editor's objects a tool context needs, on a project folder.
struct Editor {
    QTemporaryDir folder;
    EditorTabs tabs{Theme::dark()};
    SymbolIndex symbols;
    CodeNavigator navigator{&tabs, &symbols};
    McpRegistry registry;
    McpSession session{nullptr, &registry};
    QList<TerminalWidget *> terminals;
    QString message;
    QString changesShown;
    bool allow = true;
    QStringList asked;

    Editor()
    {
        DuetToolContext context;
        context.folder = [this] { return folder.path(); };
        context.tabs = &tabs;
        context.navigator = &navigator;
        context.symbols = &symbols;
        context.showMessage = [this](const QString &text) { message = text; };
        context.terminals = [this] {
            QList<std::pair<QString, TerminalWidget *>> list;
            for (TerminalWidget *terminal : terminals)
                list.append({QStringLiteral("Terminal"), terminal});
            return list;
        };
        context.addTerminal = [this](const QString &, const ProcessSpec &spec) {
            auto *terminal = new TerminalWidget;
            terminal->resize(600, 300);
            terminals.append(terminal);
            terminal->start(spec);
            return terminal;
        };
        context.showChanges = [this](const QString &path) {
            changesShown = path.isEmpty() ? QStringLiteral("*") : path;
        };
        DuetTools::registerEditorTools(registry, context);
        DuetTools::registerWorkspaceTools(registry, context);
        DuetTools::registerGitTools(registry, context);
        DuetTools::registerTerminalTools(registry, context);
#ifdef DUET_WITH_DIAGRAMS
        DuetTools::registerDiagramTools(registry, context);
#endif
        registry.setConfirmer([this](const QString &, const QString &what, std::function<void(bool)> answer) {
            asked.append(what);
            answer(allow);
        });
    }

    ~Editor() { qDeleteAll(terminals); }

    // Calls a tool and waits for its result.
    QJsonObject call(const QString &tool, const QJsonObject &arguments = {}, int timeoutMs = 10000)
    {
        static int id = 0;
        const int requestId = ++id;
        QJsonObject result;
        const QMetaObject::Connection connection =
            QObject::connect(&session, &McpSession::sent, [&result, requestId](const QJsonObject &message) {
                if (message.value(QStringLiteral("id")).toInt() == requestId)
                    result = message;
            });
        session.handleMessage(
            QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                        {QStringLiteral("id"), requestId},
                        {QStringLiteral("method"), QStringLiteral("tools/call")},
                        {QStringLiteral("params"),
                         QJsonObject{{QStringLiteral("name"), tool}, {QStringLiteral("arguments"), arguments}}}});
        QElapsedTimer clock;
        clock.start();
        while (result.isEmpty() && clock.elapsed() < timeoutMs)
            QTest::qWait(10);
        QObject::disconnect(connection);
        return result.value(QStringLiteral("result")).toObject();
    }
};

QJsonObject structured(const QJsonObject &result)
{
    return result.value(QStringLiteral("structuredContent")).toObject();
}

QString text(const QJsonObject &result)
{
    return result.value(QStringLiteral("content"))
        .toArray()
        .first()
        .toObject()
        .value(QStringLiteral("text"))
        .toString();
}

} // namespace

class TestMcpTools : public QObject {
    Q_OBJECT

private slots:
    // Files as the user has them, unsaved changes included.
    void readsTheEditor()
    {
        Editor editor;
        write(editor.folder, QStringLiteral("src/a.cpp"), "int one()\n{\n    return 1;\n}\n");
        write(editor.folder, QStringLiteral("notes.md"), "# Notes\n");
        auto *page = editor.tabs.openFile(editor.folder.filePath(QStringLiteral("src/a.cpp")));
        QVERIFY(page);
        EditorWidget *a = editor.tabs.currentEditor();
        a->setText(QStringLiteral("int one()\n{\n    return 2;\n}\n"));
        a->setSelection(2, 4, 2, 13);

        const QJsonObject state = structured(editor.call(QStringLiteral("get_editor_state")));
        QCOMPARE(state.value(QStringLiteral("file")).toString(), QStringLiteral("src/a.cpp"));
        QCOMPARE(state.value(QStringLiteral("selection")).toObject().value(QStringLiteral("text")).toString(),
                 QStringLiteral("return 2;"));
        QCOMPARE(state.value(QStringLiteral("unsavedFiles")).toArray(), QJsonArray{QStringLiteral("src/a.cpp")});

        const QJsonArray files =
            structured(editor.call(QStringLiteral("get_open_files"))).value(QStringLiteral("files")).toArray();
        QCOMPARE(files.size(), 1);
        QVERIFY(files.first().toObject().value(QStringLiteral("unsaved")).toBool());

        // The unsaved text, not the disk's; a range of lines.
        QJsonObject file = structured(
            editor.call(QStringLiteral("get_file_text"), {{QStringLiteral("path"), QStringLiteral("src/a.cpp")},
                                                          {QStringLiteral("startLine"), 3},
                                                          {QStringLiteral("endLine"), 3}}));
        QCOMPARE(file.value(QStringLiteral("text")).toString(), QStringLiteral("    return 2;"));
        QVERIFY(file.value(QStringLiteral("unsaved")).toBool());
        QVERIFY(editor.call(QStringLiteral("get_file_text"), {{QStringLiteral("path"), QStringLiteral("none.txt")}})
                    .value(QStringLiteral("isError"))
                    .toBool());

        // Search sees the unsaved text; files by fuzzy name; the outline.
        const QJsonObject found = structured(
            editor.call(QStringLiteral("search_in_files"), {{QStringLiteral("text"), QStringLiteral("return 2")}}));
        QCOMPARE(found.value(QStringLiteral("matches")).toArray().size(), 1);
        QCOMPARE(
            found.value(QStringLiteral("matches")).toArray().first().toObject().value(QStringLiteral("line")).toInt(),
            3);
        QCOMPARE(
            structured(editor.call(QStringLiteral("find_files"), {{QStringLiteral("query"), QStringLiteral("acpp")}}))
                .value(QStringLiteral("files"))
                .toArray()
                .first()
                .toString(),
            QStringLiteral("src/a.cpp"));
        const QJsonArray symbols = structured(editor.call(QStringLiteral("get_document_symbols"),
                                                          {{QStringLiteral("path"), QStringLiteral("src/a.cpp")}}))
                                       .value(QStringLiteral("symbols"))
                                       .toArray();
        QCOMPARE(symbols.first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("one"));
        QCOMPARE(symbols.first().toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("function"));

        // Opening at a line, saving.
        QVERIFY(!editor
                     .call(QStringLiteral("open_file"),
                           {{QStringLiteral("path"), QStringLiteral("notes.md")}, {QStringLiteral("line"), 1}})
                     .value(QStringLiteral("isError"))
                     .toBool());
        QCOMPARE(QFileInfo(editor.tabs.currentFilePath()).fileName(), QStringLiteral("notes.md"));
        editor.call(QStringLiteral("save_files"), {{QStringLiteral("paths"), QJsonArray{QStringLiteral("src/a.cpp")}}});
        QFile saved(editor.folder.filePath(QStringLiteral("src/a.cpp")));
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QVERIFY(saved.readAll().contains("return 2;"));

        editor.call(QStringLiteral("notify_user"), {{QStringLiteral("message"), QStringLiteral("Done")}});
        QCOMPARE(editor.message, QStringLiteral("Done"));
    }

    // Git, and the snapshots that need the user's agreement to restore.
    void readsAndActsOnGit()
    {
        Editor editor;
        write(editor.folder, QStringLiteral("a.txt"), "one\n");
        Git::run(editor.folder.path(), {"init", "-q", "-b", "main"});
        Git::run(editor.folder.path(), {"add", "-A"});
        Git::run(editor.folder.path(), {"-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "init"});
        write(editor.folder, QStringLiteral("a.txt"), "two\n");

        QJsonObject status =
            structured(editor.call(QStringLiteral("get_git_status"), {{QStringLiteral("patches"), true}}));
        QCOMPARE(status.value(QStringLiteral("branch")).toString(), QStringLiteral("main"));
        const QJsonObject change = status.value(QStringLiteral("unstaged")).toArray().first().toObject();
        QCOMPARE(change.value(QStringLiteral("path")).toString(), QStringLiteral("a.txt"));
        QVERIFY(change.value(QStringLiteral("patch")).toString().contains(QStringLiteral("+two")));
        QCOMPARE(structured(editor.call(QStringLiteral("get_file_history")))
                     .value(QStringLiteral("commits"))
                     .toArray()
                     .first()
                     .toObject()
                     .value(QStringLiteral("summary"))
                     .toString(),
                 QStringLiteral("init"));

        QVERIFY(
            text(editor.call(QStringLiteral("create_snapshot"), {{QStringLiteral("reason"), QStringLiteral("test")}}))
                .startsWith(QStringLiteral("Snapshot ")));
        const QJsonArray snapshots =
            structured(editor.call(QStringLiteral("list_snapshots"))).value(QStringLiteral("snapshots")).toArray();
        QCOMPARE(snapshots.size(), 1);
        const QString id = snapshots.first().toObject().value(QStringLiteral("id")).toString();
        write(editor.folder, QStringLiteral("a.txt"), "three\n");

        // Refused: nothing happens.
        editor.allow = false;
        QVERIFY(editor.call(QStringLiteral("restore_snapshot"), {{QStringLiteral("id"), id}})
                    .value(QStringLiteral("isError"))
                    .toBool());
        QCOMPARE(editor.asked.last(), QStringLiteral("restore the files of snapshot %1").arg(id));
        QFile file(editor.folder.filePath(QStringLiteral("a.txt")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("three\n"));
        file.close();
        // Allowed: the snapshot's file is back.
        editor.allow = true;
        QVERIFY(!editor.call(QStringLiteral("restore_snapshot"), {{QStringLiteral("id"), id}})
                     .value(QStringLiteral("isError"))
                     .toBool());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("two\n"));
        file.close();

        // Stage and commit, each asked.
        editor.call(QStringLiteral("stage_files"), {{QStringLiteral("paths"), QJsonArray{QStringLiteral("a.txt")}}});
        status = structured(editor.call(QStringLiteral("get_git_status")));
        QCOMPARE(status.value(QStringLiteral("staged")).toArray().size(), 1);
        editor.call(QStringLiteral("commit"), {{QStringLiteral("message"), QStringLiteral("Second\n\nBody")}});
        QCOMPARE(editor.asked.last(), QStringLiteral("commit “Second”"));
        QCOMPARE(structured(editor.call(QStringLiteral("get_file_history"),
                                        {{QStringLiteral("path"), QStringLiteral("a.txt")}}))
                     .value(QStringLiteral("commits"))
                     .toArray()
                     .first()
                     .toObject()
                     .value(QStringLiteral("summary"))
                     .toString(),
                 QStringLiteral("Second"));

        editor.call(QStringLiteral("show_changes"), {{QStringLiteral("path"), QStringLiteral("a.txt")}});
        QCOMPARE(editor.changesShown, QStringLiteral("a.txt"));
    }

    // Branches, a remote, stashes and merges; what changes is asked first.
    void actsOnBranchesAndRemotes()
    {
        QTemporaryFile config;
        QVERIFY(config.open());
        qputenv("GIT_CONFIG_GLOBAL", config.fileName().toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        for (const char *name : {"GIT_AUTHOR_NAME", "GIT_COMMITTER_NAME", "GIT_AUTHOR_EMAIL", "GIT_COMMITTER_EMAIL"})
            qputenv(name, "t");

        QTemporaryDir bare;
        Git::run(bare.path(), {"init", "-q", "--bare", "-b", "main"});
        Editor editor;
        write(editor.folder, QStringLiteral("a.txt"), "one\n");
        Git::run(editor.folder.path(), {"init", "-q", "-b", "main"});
        Git::run(editor.folder.path(), {"add", "-A"});
        Git::run(editor.folder.path(), {"commit", "-qm", "init"});
        Git::run(editor.folder.path(), {"remote", "add", "origin", bare.path()});

        // Publishing main: asked, then tracked.
        editor.call(QStringLiteral("push"));
        QCOMPARE(editor.asked.last(), QStringLiteral("push main"));
        QJsonObject status = structured(editor.call(QStringLiteral("get_git_status")));
        QCOMPARE(status.value(QStringLiteral("upstream")).toString(), QStringLiteral("origin/main"));
        QCOMPARE(status.value(QStringLiteral("ahead")).toInt(), 0);
        QVERIFY(!editor.call(QStringLiteral("fetch")).value(QStringLiteral("isError")).toBool());

        // A new branch, refused then allowed.
        editor.allow = false;
        editor.call(QStringLiteral("switch_branch"),
                    {{QStringLiteral("name"), QStringLiteral("topic")}, {QStringLiteral("create"), true}});
        QCOMPARE(editor.asked.last(), QStringLiteral("create and switch to topic"));
        QCOMPARE(RepositoryInfo::read(editor.folder.path())->branch, QStringLiteral("main"));
        editor.allow = true;
        editor.call(QStringLiteral("switch_branch"),
                    {{QStringLiteral("name"), QStringLiteral("topic")}, {QStringLiteral("create"), true}});
        QCOMPARE(RepositoryInfo::read(editor.folder.path())->branch, QStringLiteral("topic"));
        const QJsonArray branches =
            structured(editor.call(QStringLiteral("list_branches"))).value(QStringLiteral("branches")).toArray();
        QCOMPARE(branches.size(), 3); // main, topic, origin/main
        QVERIFY(branches.at(1).toObject().value(QStringLiteral("current")).toBool());
        QVERIFY(branches.at(2).toObject().value(QStringLiteral("remote")).toBool());

        // Stash and bring back.
        write(editor.folder, QStringLiteral("a.txt"), "two\n");
        editor.call(QStringLiteral("stash_changes"), {{QStringLiteral("message"), QStringLiteral("wip")}});
        QCOMPARE(editor.asked.last(), QStringLiteral("stash the uncommitted changes as “wip”"));
        const QJsonArray stashes =
            structured(editor.call(QStringLiteral("list_stashes"), {{QStringLiteral("patches"), true}}))
                .value(QStringLiteral("stashes"))
                .toArray();
        QCOMPARE(stashes.size(), 1);
        QVERIFY(stashes.first().toObject().value(QStringLiteral("patch")).toString().contains(QStringLiteral("+two")));
        editor.call(QStringLiteral("apply_stash"), {{QStringLiteral("index"), 0}});
        QCOMPARE(editor.asked.last(), QStringLiteral("pop stash@{0}"));
        QFile file(editor.folder.filePath(QStringLiteral("a.txt")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("two\n"));
        file.close();

        // A conflicting merge, then aborted.
        Git::run(editor.folder.path(), {"commit", "-qam", "topic"});
        Git::run(editor.folder.path(), {"switch", "-q", "main"});
        write(editor.folder, QStringLiteral("a.txt"), "three\n");
        Git::run(editor.folder.path(), {"commit", "-qam", "main"});
        QVERIFY(editor.call(QStringLiteral("merge_branch"), {{QStringLiteral("branch"), QStringLiteral("topic")}})
                    .value(QStringLiteral("isError"))
                    .toBool());
        QCOMPARE(editor.asked.last(), QStringLiteral("merge into the current branch topic"));
        status = structured(editor.call(QStringLiteral("get_git_status")));
        QCOMPARE(status.value(QStringLiteral("operation")).toString(), QStringLiteral("merge"));
        QCOMPARE(status.value(QStringLiteral("ahead")).toInt(), 1);
        editor.call(QStringLiteral("abort_operation"));
        QVERIFY(RepositoryInfo::read(editor.folder.path())->operation.isEmpty());
    }

    // A command in a terminal the user sees, once allowed.
    void runsCommandsInATerminal()
    {
        Editor editor;
        // The same in sh and PowerShell.
        const QJsonObject result =
            structured(editor.call(QStringLiteral("run_in_terminal"),
                                   {{QStringLiteral("command"), QStringLiteral("echo hello; exit 3")}}, 20000));
        QVERIFY(result.value(QStringLiteral("finished")).toBool());
        QCOMPARE(result.value(QStringLiteral("exitCode")).toInt(), 3);
        QVERIFY(result.value(QStringLiteral("output")).toString().contains(QStringLiteral("hello")));
        QVERIFY(editor.asked.last().startsWith(QStringLiteral("run “echo hello")));
        const QJsonArray terminals =
            structured(editor.call(QStringLiteral("list_terminals"))).value(QStringLiteral("terminals")).toArray();
        QCOMPARE(terminals.size(), 1);
        QVERIFY(structured(editor.call(QStringLiteral("read_terminal"), {{QStringLiteral("id"), 0}}))
                    .value(QStringLiteral("text"))
                    .toString()
                    .contains(QStringLiteral("hello")));
    }

#ifdef DUET_WITH_DIAGRAMS
    // A diagram the user looks at: its content and selection, changes made
    // in the open diagram.
    void readsAndChangesDiagrams()
    {
        Editor editor;
        write(editor.folder, QStringLiteral("flow.mmd"), "flowchart TB\n  A[Start] --> B[End]\n");
        QJsonObject diagram = structured(
            editor.call(QStringLiteral("get_diagram"), {{QStringLiteral("path"), QStringLiteral("flow.mmd")}}));
        QCOMPARE(diagram.value(QStringLiteral("family")).toString(), QStringLiteral("flowchart"));
        QCOMPARE(diagram.value(QStringLiteral("elements")).toArray().size(), 2);
        QVERIFY(!diagram.value(QStringLiteral("open")).toBool());

        // Selected by key, then read back as the user's selection.
        editor.call(QStringLiteral("select_diagram_elements"),
                    {{QStringLiteral("path"), QStringLiteral("flow.mmd")},
                     {QStringLiteral("keys"), QJsonArray{QStringLiteral("B")}}});
        DiagramView *view = editor.tabs.diagramViewFor(editor.folder.filePath(QStringLiteral("flow.mmd")));
        QVERIFY(view);
        diagram = structured(
            editor.call(QStringLiteral("get_diagram"), {{QStringLiteral("path"), QStringLiteral("flow.mmd")}}));
        QVERIFY(diagram.value(QStringLiteral("open")).toBool());
        int selected = 0;
        for (const QJsonValue &element : diagram.value(QStringLiteral("elements")).toArray())
            selected += element.toObject().value(QStringLiteral("selected")).toBool();
        QCOMPARE(selected, 1);

        // A source applied in the editor, one step of history, unsaved; the
        // elements already there keep their place.
        const diagram::Shape *start = view->board()->shapeByReference(QStringLiteral("A"));
        const QPointF startPlace(start->x, start->y);
        const QJsonObject applied = structured(editor.call(
            QStringLiteral("apply_diagram_source"),
            {{QStringLiteral("path"), QStringLiteral("flow.mmd")},
             {QStringLiteral("source"), QStringLiteral("flowchart TB\n  A[Start] --> B[End]\n  B --> C{Again?}\n")}}));
        QCOMPARE(applied.value(QStringLiteral("elements")).toInt(), 3);
        start = view->board()->shapeByReference(QStringLiteral("A"));
        QVERIFY2(
            std::abs(start->x - startPlace.x()) < 0.2 && std::abs(start->y - startPlace.y()) < 0.2,
            qPrintable(
                QStringLiteral("%1,%2 -> %3,%4").arg(startPlace.x()).arg(startPlace.y()).arg(start->x).arg(start->y)));
        QVERIFY(view->isModified());
        QVERIFY(view->board()->canUndo());
        QVERIFY(
            editor
                .call(QStringLiteral("apply_diagram_source"), {{QStringLiteral("path"), QStringLiteral("flow.mmd")},
                                                               {QStringLiteral("source"), QStringLiteral("nonsense")}})
                .value(QStringLiteral("isError"))
                .toBool());

        QVERIFY(!editor
                     .call(QStringLiteral("layout_diagram"), {{QStringLiteral("path"), QStringLiteral("flow.mmd")},
                                                              {QStringLiteral("direction"), QStringLiteral("LR")}})
                     .value(QStringLiteral("isError"))
                     .toBool());
        QCOMPARE(view->board()->meta.value(QStringLiteral("rankdir")).toString(), QStringLiteral("LR"));

        QVERIFY(!editor
                     .call(QStringLiteral("export_diagram"), {{QStringLiteral("path"), QStringLiteral("flow.mmd")},
                                                              {QStringLiteral("output"), QStringLiteral("flow.png")}})
                     .value(QStringLiteral("isError"))
                     .toBool());
        QVERIFY(QFileInfo(editor.folder.filePath(QStringLiteral("flow.png"))).size() > 1000);

        QVERIFY(structured(editor.call(QStringLiteral("get_diagram_problems"),
                                       {{QStringLiteral("path"), QStringLiteral("flow.mmd")}}))
                    .value(QStringLiteral("problems"))
                    .toArray()
                    .isEmpty());
        QVERIFY(!structured(editor.call(QStringLiteral("list_diagram_kinds")))
                     .value(QStringLiteral("kinds"))
                     .toArray()
                     .isEmpty());
    }
#endif
};

QTEST_MAIN(TestMcpTools)
#include "tst_mcptools.moc"
