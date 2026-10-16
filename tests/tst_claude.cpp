#include "claude/ClaudeActivity.h"
#include "claude/ClaudeLauncher.h"
#include "claude/ClaudeUsage.h"
#include "claude/HookEvent.h"
#include "claude/HookServer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
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

    void parsesStopFailure()
    {
        const auto event = HookEvent::fromJson(R"({"hook_event_name":"StopFailure","error":"overloaded"})");
        QVERIFY(event);
        QCOMPARE(event->kind, HookEvent::Kind::ResponseFinished);
        QVERIFY(event->failed);
        QCOMPARE(event->message, QStringLiteral("overloaded"));
    }

    void parsesPromptPermissionAndCommand()
    {
        const auto prompt = HookEvent::fromJson(R"({"hook_event_name":"UserPromptSubmit","prompt":"fix it"})");
        QVERIFY(prompt);
        QCOMPARE(prompt->kind, HookEvent::Kind::PromptSubmitted);
        QCOMPARE(prompt->prompt, QStringLiteral("fix it"));

        const auto permission = HookEvent::fromJson(R"({"hook_event_name":"PermissionRequest","tool_name":"Bash"})");
        QVERIFY(permission);
        QCOMPARE(permission->kind, HookEvent::Kind::PermissionRequested);
        QCOMPARE(permission->message, QStringLiteral("Bash"));

        const auto command = HookEvent::fromJson(
            R"({"hook_event_name":"PostToolUse","tool_name":"Bash","tool_input":{"command":"make"}})");
        QVERIFY(command);
        QCOMPARE(command->kind, HookEvent::Kind::CommandFinished);
    }

    void keepsOnlyNotificationsThatWaitForTheUser()
    {
        const auto permission = HookEvent::fromJson(
            R"({"hook_event_name":"Notification","notification_type":"permission_prompt","message":"Allow?"})");
        QVERIFY(permission);
        QCOMPARE(permission->kind, HookEvent::Kind::NeedsAttention);
        QCOMPARE(permission->message, QStringLiteral("Allow?"));
        QVERIFY(!HookEvent::fromJson(R"({"hook_event_name":"Notification","notification_type":"idle_prompt"})"));
        QVERIFY(!HookEvent::fromJson(R"({"hook_event_name":"Notification"})"));
    }

    void ignoresUnknownOrInvalidEvents()
    {
        QVERIFY(!HookEvent::fromJson(R"({"hook_event_name":"SessionStart"})"));
        QVERIFY(!HookEvent::fromJson("not json"));
        QVERIFY(!HookEvent::fromJson(R"({"hook_event_name":"PostToolUse","tool_input":{}})"));
    }

    void settingsDeclareHooksAndStatusLine()
    {
        const QJsonObject settings =
            QJsonDocument::fromJson(ClaudeLauncher::sessionSettings("\"/opt/enlumine-hook\"")).object();
        const QJsonObject hooks = settings["hooks"].toObject();
        QCOMPARE(hooks.keys(), (QStringList{"Notification", "PermissionRequest", "PostToolUse", "Stop", "StopFailure",
                                            "UserPromptSubmit"}));
        const QJsonObject postToolUse = hooks["PostToolUse"].toArray().first().toObject();
        QCOMPARE(postToolUse["matcher"].toString(), QStringLiteral("Edit|Write|MultiEdit|NotebookEdit|Bash"));
        const QJsonObject command = postToolUse["hooks"].toArray().first().toObject();
        QCOMPARE(command["type"].toString(), QStringLiteral("command"));
        QCOMPARE(command["command"].toString(), QStringLiteral("\"/opt/enlumine-hook\""));
        QCOMPARE(settings["statusLine"].toObject()["command"].toString(),
                 QStringLiteral("\"/opt/enlumine-hook\" statusline"));
    }

    void findsTheUserStatusLine()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        QVERIFY(QDir(project.path()).mkdir(".claude"));
        QFile local(project.filePath(".claude/settings.local.json"));
        QVERIFY(local.open(QIODevice::WriteOnly));
        local.write(R"({"statusLine":{"type":"command","command":"~/bin/line.sh"}})");
        local.close();
        qputenv("CLAUDE_CONFIG_DIR", project.filePath("no-config").toUtf8());
        QCOMPARE(ClaudeLauncher::userStatusLineCommand(project.path()), QStringLiteral("~/bin/line.sh"));
        qunsetenv("CLAUDE_CONFIG_DIR");
    }

    void readsUsageFromStatusLine()
    {
        const QJsonObject status = QJsonDocument::fromJson(R"({
            "model": {"display_name": "Opus"},
            "context_window": {"used_percentage": 41.6},
            "cost": {"total_cost_usd": 1.25, "total_lines_added": 10, "total_lines_removed": 2},
            "rate_limits": {"five_hour": {"used_percentage": 80, "resets_at": 1790000000},
                            "seven_day": {"used_percentage": 12}}
        })").object();
        const ClaudeUsage usage = ClaudeUsage::fromStatusJson(status);
        QCOMPARE(usage.model, QStringLiteral("Opus"));
        QCOMPARE(usage.summary(), QStringLiteral("Context 42% · 5h 80% · 7d 12%"));
        QCOMPARE(usage.highestPercent(), 80.0);
        QCOMPARE(usage.fiveHourReset, QDateTime::fromSecsSinceEpoch(1790000000));
        QVERIFY(usage.details().contains("Opus"));
        QVERIFY(ClaudeUsage::fromStatusJson({}).summary().isEmpty());
    }

    void activityFollowsTheResponse()
    {
        ClaudeActivity activity;
        QSignalSpy attention(&activity, &ClaudeActivity::attentionRequested);
        HookEvent event{};

        event.kind = HookEvent::Kind::PromptSubmitted;
        activity.handle(event);
        QCOMPARE(activity.state(), ClaudeActivity::State::Working);

        event.kind = HookEvent::Kind::PermissionRequested;
        event.message = "Bash";
        activity.handle(event);
        QCOMPARE(activity.state(), ClaudeActivity::State::NeedsInput);
        event.kind = HookEvent::Kind::NeedsAttention; // same wait, reported again
        activity.handle(event);
        QCOMPARE(attention.size(), 1);

        event.kind = HookEvent::Kind::CommandFinished; // permission granted
        activity.handle(event);
        QCOMPARE(activity.state(), ClaudeActivity::State::Working);

        event.kind = HookEvent::Kind::ResponseFinished;
        activity.handle(event);
        QCOMPARE(activity.state(), ClaudeActivity::State::Done);
        QCOMPARE(attention.size(), 2);

        activity.setWatched(true); // the user looks: acknowledged
        QCOMPARE(activity.state(), ClaudeActivity::State::Idle);
    }

    void watchedActivityDoesNotAlert()
    {
        ClaudeActivity activity;
        QSignalSpy attention(&activity, &ClaudeActivity::attentionRequested);
        activity.setWatched(true);
        HookEvent event{};
        event.kind = HookEvent::Kind::PromptSubmitted;
        activity.handle(event);
        activity.interrupt();
        QCOMPARE(activity.state(), ClaudeActivity::State::Idle);
        activity.handle(event);
        event.kind = HookEvent::Kind::ResponseFinished;
        activity.handle(event);
        QCOMPARE(activity.state(), ClaudeActivity::State::Idle);
        QCOMPARE(attention.size(), 0);
    }

    // End to end: the real enlumine-hook binary, launched as Claude Code
    // would, relays the event to the editor's server.
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
        hook.write(R"({"hook_event_name":"Stop","session_id":"hello"})");
        hook.closeWriteChannel();

        QTRY_COMPARE(received.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(hook.state(), QProcess::NotRunning, 5000);
        QCOMPARE(hook.exitCode(), 0);
        QVERIFY(hook.readAllStandardOutput().isEmpty()); // nothing must end up in Claude's context
        QCOMPARE(received.first().first().value<HookEvent>().sessionId, QStringLiteral("hello"));
    }

    void statusLineIsForwardedAndPrinted()
    {
        HookServer server;
        QVERIFY(server.listen());
        QSignalSpy received(&server, &HookServer::hookReceived);

        QProcess hook;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("ENLUMINE_SOCKET", server.serverName());
        environment.remove("ENLUMINE_STATUSLINE_COMMAND");
        hook.setProcessEnvironment(environment);
        hook.start(QStringLiteral(ENLUMINE_HOOK_PATH), {"statusline"});
        QVERIFY(hook.waitForStarted());
        hook.write(R"({"model":{"display_name":"Opus"},"context_window":{"used_percentage":42}})");
        hook.closeWriteChannel();

        QTRY_COMPARE(received.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(hook.state(), QProcess::NotRunning, 5000);
        const HookEvent event = received.first().first().value<HookEvent>();
        QCOMPARE(event.kind, HookEvent::Kind::StatusUpdated);
        QCOMPARE(event.status["model"].toObject()["display_name"].toString(), QStringLiteral("Opus"));
        QCOMPARE(hook.readAllStandardOutput().trimmed(), QByteArray("Opus · context 42%"));
    }

    void hookProgramNeverFailsWithoutEditor()
    {
        QProcess hook;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("ENLUMINE_SOCKET", "enlumine-nonexistent-server");
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
