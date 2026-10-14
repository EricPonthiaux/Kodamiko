#include "claude/ClaudeLauncher.h"
#include "claude/HookEvent.h"
#include "claude/HookServer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

class TestClaude : public QObject {
    Q_OBJECT

private slots:
    void parsesEditedFiles()
    {
        const auto event = HookEvent::fromJson(
            R"({"hook_event_name":"PostToolUse","tool_name":"Edit","tool_input":{"file_path":"/p/a.cpp"}})");
        QVERIFY(event);
        QCOMPARE(event->kind, HookEvent::Kind::FilesEdited);
        QCOMPARE(event->filePaths, QStringList{"/p/a.cpp"});
    }

    void parsesStop()
    {
        const auto event = HookEvent::fromJson(R"({"hook_event_name":"Stop","session_id":"s1"})");
        QVERIFY(event);
        QCOMPARE(event->kind, HookEvent::Kind::ResponseFinished);
        QCOMPARE(event->sessionId, QStringLiteral("s1"));
    }

    void ignoresUnknownOrInvalidEvents()
    {
        QVERIFY(!HookEvent::fromJson(R"({"hook_event_name":"Notification"})"));
        QVERIFY(!HookEvent::fromJson(R"({"hook_event_name":"UserPromptSubmit","prompt":"x"})"));
        QVERIFY(!HookEvent::fromJson("pas du json"));
        QVERIFY(!HookEvent::fromJson(R"({"hook_event_name":"PostToolUse","tool_input":{}})"));
    }

    void settingsDeclareTheTwoHooks()
    {
        const QJsonObject hooks =
            QJsonDocument::fromJson(ClaudeLauncher::hooksSettings("\"/opt/enlumine-hook\"")).object()["hooks"].toObject();
        QCOMPARE(hooks.keys(), (QStringList{"PostToolUse", "Stop"}));
        const QJsonObject postToolUse = hooks["PostToolUse"].toArray().first().toObject();
        QCOMPARE(postToolUse["matcher"].toString(), QStringLiteral("Edit|Write|MultiEdit|NotebookEdit"));
        const QJsonObject command = postToolUse["hooks"].toArray().first().toObject();
        QCOMPARE(command["type"].toString(), QStringLiteral("command"));
        QCOMPARE(command["command"].toString(), QStringLiteral("\"/opt/enlumine-hook\""));
    }

    // De bout en bout : le vrai binaire enlumine-hook, lancé comme le ferait
    // Claude Code, relaie l'événement jusqu'au serveur de l'éditeur.
    void hookProgramRelaysEventsToTheServer()
    {
        HookServer server;
        QVERIFY(server.listen());
        QSignalSpy received(&server, &HookServer::hookReceived);

        QProcess hook;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("ENLUMINE_SOCKET", server.serverName());
        hook.setProcessEnvironment(environment);
        hook.start(QStringLiteral(ENLUMINE_HOOK_PATH), {});
        QVERIFY(hook.waitForStarted());
        hook.write(R"({"hook_event_name":"Stop","session_id":"bonjour"})");
        hook.closeWriteChannel();

        QTRY_COMPARE(received.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(hook.state(), QProcess::NotRunning, 5000);
        QCOMPARE(hook.exitCode(), 0);
        QVERIFY(hook.readAllStandardOutput().isEmpty()); // rien ne doit partir dans le contexte de Claude
        QCOMPARE(received.first().first().value<HookEvent>().sessionId, QStringLiteral("bonjour"));
    }

    void hookProgramNeverFailsWithoutEditor()
    {
        QProcess hook;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("ENLUMINE_SOCKET", "enlumine-serveur-inexistant");
        hook.setProcessEnvironment(environment);
        hook.start(QStringLiteral(ENLUMINE_HOOK_PATH), {});
        QVERIFY(hook.waitForStarted());
        hook.write(R"({"hook_event_name":"Stop"})");
        hook.closeWriteChannel();
        QVERIFY(hook.waitForFinished(5000));
        QCOMPARE(hook.exitCode(), 0);
    }
};

QTEST_GUILESS_MAIN(TestClaude)
#include "tst_claude.moc"
