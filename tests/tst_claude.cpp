// SPDX-License-Identifier: GPL-3.0-only

#include "claude/AgentCheckPopup.h"
#include "claude/AgentSessions.h"
#include "claude/ClaudeActivity.h"
#include "claude/ClaudeConfig.h"
#include "claude/ClaudeConfigView.h"
#include "claude/ClaudeLauncher.h"
#include "claude/ClaudeLint.h"
#include "claude/ClaudeUsage.h"
#include "claude/CloseAgentPopup.h"
#include "claude/CodeRequest.h"
#include "claude/CommitMessage.h"
#include "claude/DiagramRequest.h"
#include "claude/EditorContext.h"
#include "claude/EffectiveSettings.h"
#include "claude/HookEvent.h"
#include "claude/HookServer.h"
#include "claude/NewAgentPopup.h"
#include "claude/UnsavedEditGuard.h"
#include "editor/EditorWidget.h"
#include "terminal/TerminalWidget.h"
#include "ui/MessagePopup.h"
#include "ui/Theme.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTreeWidget>
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

    void parsesEditRequests()
    {
        const auto edit = HookEvent::fromJson(
            R"({"hook_event_name":"PreToolUse","tool_name":"Write","tool_input":{"file_path":"/p/a.cpp"}})");
        QVERIFY(edit);
        QCOMPARE(edit->kind, HookEvent::Kind::EditRequested);
        QCOMPARE(edit->message, QStringLiteral("Write"));
        QCOMPARE(edit->filePaths, QStringList{"/p/a.cpp"});
        QVERIFY(!HookEvent::fromJson(R"({"hook_event_name":"PreToolUse","tool_name":"Read","tool_input":{}})"));
    }

    void refusesEditsOverUnsavedChanges()
    {
        const QString reason = UnsavedEditGuard::reason({"src/a.cpp", "src/b.h"});
        QVERIFY(reason.startsWith(QStringLiteral("src/a.cpp, src/b.h have unsaved changes")));
        QVERIFY(UnsavedEditGuard::reason({"a.cpp"}).startsWith(QStringLiteral("a.cpp has unsaved changes")));
        const QJsonObject output = UnsavedEditGuard::denyOutput(reason)["hookSpecificOutput"].toObject();
        QCOMPARE(output["hookEventName"].toString(), QStringLiteral("PreToolUse"));
        QCOMPARE(output["permissionDecision"].toString(), QStringLiteral("deny"));
        QCOMPARE(output["permissionDecisionReason"].toString(), reason);
    }

    void asksClaudeForADiagram()
    {
        const QString prompt =
            DiagramRequest::prompt("Flowchart", "docs/diagrams/returns.mmd", "  How returns work \n");
        QVERIFY(prompt.startsWith(QStringLiteral("Draw a diagram in the file docs/diagrams/returns.mmd, which does not "
                                                 "exist yet, following the duet:diagrams skill. Kind of diagram: "
                                                 "Flowchart.\n")));
        QVERIFY(prompt.contains(QStringLiteral("<request>\nHow returns work\n</request>")));

        const QStringList arguments = DiagramRequest::arguments(prompt, "docs/diagrams/returns.mmd",
                                                                "/opt/duet/claude-plugin", "\"/opt/duet-hook\"");
        QCOMPARE(arguments.first(), prompt); // before the variadic --allowedTools
        QCOMPARE(arguments.at(arguments.indexOf("--output-format") + 1), QStringLiteral("stream-json"));
        QVERIFY(arguments.contains("--verbose"));
        QCOMPARE(arguments.at(arguments.indexOf("--plugin-dir") + 1),
                 QDir::toNativeSeparators(QStringLiteral("/opt/duet/claude-plugin")));
        QCOMPARE(arguments.at(arguments.indexOf("--tools") + 1), QStringLiteral("Read,Grep,Glob,Skill,Write,Edit"));
        QCOMPARE(arguments.mid(arguments.indexOf("--allowedTools") + 1),
                 (QStringList{"Read", "Grep", "Glob", "Skill", "Edit(./docs/diagrams/returns.mmd)"}));
        const QJsonObject settings =
            QJsonDocument::fromJson(arguments.at(arguments.indexOf("--settings") + 1).toUtf8()).object();
        const QJsonObject check = settings["hooks"].toObject()["PostToolUse"].toArray().first().toObject();
        QCOMPARE(check["matcher"].toString(), QStringLiteral("Write|Edit|MultiEdit"));
        QCOMPARE(check["hooks"].toArray().first().toObject()["command"].toString(),
                 QStringLiteral("\"/opt/duet-hook\""));

        const QStringList bare = DiagramRequest::arguments(prompt, "a.flow", {}, {});
        QVERIFY(!bare.contains("--plugin-dir"));
        QVERIFY(!bare.contains("--settings"));
    }

    void followsClaudeDrawing()
    {
        using Kind = DiagramRequest::Event::Kind;
        const auto steps = DiagramRequest::parseLine(
            R"({"type":"assistant","message":{"content":[{"type":"text","text":"Let me look."},)"
            R"({"type":"tool_use","name":"Read","input":{"file_path":"/p/src/a.cpp"}},)"
            R"({"type":"tool_use","name":"Grep","input":{"pattern":"order"}},)"
            R"({"type":"tool_use","name":"Write","input":{"file_path":"/p/docs/a.flow"}}]}})",
            "/p");
        QCOMPARE(steps.kind, Kind::Steps);
        QCOMPARE(steps.steps, (QStringList{QDir::toNativeSeparators("Reading src/a.cpp"),
                                           QStringLiteral("Searching for \u201corder\u201d"),
                                           QDir::toNativeSeparators("Drawing docs/a.flow")}));
        QCOMPARE(
            DiagramRequest::parseLine(R"({"type":"assistant","message":{"content":[{"type":"text"}]}})", "/p").kind,
            Kind::None);
        QCOMPARE(DiagramRequest::parseLine(R"({"type":"system","subtype":"init"})", "/p").kind, Kind::None);

        const auto done = DiagramRequest::parseLine(
            R"({"type":"result","subtype":"success","is_error":false,"result":" The diagram shows returns. "})", "/p");
        QCOMPARE(done.kind, Kind::Finished);
        QVERIFY(!done.failed);
        QCOMPARE(done.text, QStringLiteral("The diagram shows returns."));
        const auto limit =
            DiagramRequest::parseLine(R"({"type":"result","subtype":"error_max_turns","is_error":true})", "/p");
        QVERIFY(limit.failed);
        QCOMPARE(limit.text, QStringLiteral("error_max_turns"));
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
            QJsonDocument::fromJson(ClaudeLauncher::sessionSettings("\"/opt/duet-hook\"")).object();
        const QJsonObject hooks = settings["hooks"].toObject();
        QCOMPARE(hooks.keys(), (QStringList{"Notification", "PermissionRequest", "PostToolUse", "PreToolUse", "Stop",
                                            "StopFailure", "UserPromptSubmit"}));
        QCOMPARE(hooks["PreToolUse"].toArray().first().toObject()["matcher"].toString(),
                 QStringLiteral("Edit|Write|MultiEdit|NotebookEdit"));
        const QJsonObject postToolUse = hooks["PostToolUse"].toArray().first().toObject();
        QCOMPARE(postToolUse["matcher"].toString(), QStringLiteral("Edit|Write|MultiEdit|NotebookEdit|Bash"));
        const QJsonObject command = postToolUse["hooks"].toArray().first().toObject();
        QCOMPARE(command["type"].toString(), QStringLiteral("command"));
        QCOMPARE(command["command"].toString(), QStringLiteral("\"/opt/duet-hook\""));
        QCOMPARE(settings["statusLine"].toObject()["command"].toString(),
                 QStringLiteral("\"/opt/duet-hook\" statusline"));
    }

    void settingsAcceptMessagesFromOtherSessions()
    {
        const QJsonObject settings =
            QJsonDocument::fromJson(ClaudeLauncher::sessionSettings("\"/opt/duet-hook\"")).object();
        QCOMPARE(settings["crossSessionInbound"].toString(), QStringLiteral("accept"));
    }

    void agentHooksDriveTheAgentsActivityOnly()
    {
        AgentSessions agents;
        TerminalWidget terminal;
        agents.add({.name = "agent-1"}, "agent-session", &terminal);
        const HookEvent agentPrompt{.kind = HookEvent::Kind::PromptSubmitted, .sessionId = "agent-session"};
        const HookEvent mainPrompt{.kind = HookEvent::Kind::PromptSubmitted, .sessionId = "main-session"};

        agents.handle(agentPrompt);

        QVERIFY(agents.owns(agentPrompt));
        QVERIFY(!agents.owns(mainPrompt));
        QCOMPARE(agents.find(&terminal)->activity->state(), ClaudeActivity::State::Working);
    }

    void statusLineEventNamesItsSession()
    {
        const std::optional<HookEvent> event = HookEvent::fromJson(
            R"({"hook_event_name":"DuetStatusLine","status":{"session_id":"agent-session","model":{}}})");

        QVERIFY(event);
        QCOMPARE(event->sessionId, QStringLiteral("agent-session"));
    }

    void forgetsAnAgentWhenItsTerminalIsDestroyed()
    {
        AgentSessions agents;
        auto terminal = std::make_unique<TerminalWidget>();
        agents.add({.name = "agent-1"}, "agent-session", terminal.get());

        terminal.reset();

        QVERIFY(!agents.owns({.kind = HookEvent::Kind::PromptSubmitted, .sessionId = "agent-session"}));
    }

    void newAgentPopupChecksTheName()
    {
        QWidget window;
        NewAgentPopup popup(Theme::dark(), &window);
        popup.load("agent-1", NewAgentPopup::UncommittedChanges::None);
        QSignalSpy requested(&popup, &NewAgentPopup::startRequested);
        QVERIFY(popup.startButton()->isEnabled()); // the suggested name

        popup.nameField()->clear();
        QTest::keyClicks(popup.nameField(), "fix login");
        QCOMPARE(popup.nameField()->text(), QString("fix-login")); // as typed
        QVERIFY(popup.message()->text().contains("duet/fix-login"));
        popup.nameField()->setText("bad..name");
        QVERIFY(!popup.startButton()->isEnabled());

        popup.nameField()->setText("reviewer");
        popup.start();

        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.first().at(0).toString(), QString("reviewer"));
    }

    void closeAgentPopupReportsTheChosenFateOfTheWorktreeAskedFor()
    {
        QWidget window;
        CloseAgentPopup popup(Theme::dark(), &window);
        popup.load({.name = "agent-1", .path = "/repo/.claude/worktrees/agent-1", .branch = "duet/agent-1"});
        QString chosenName;
        std::optional<CloseAgentPopup::WorktreeFate> chosenFate;
        connect(&popup, &CloseAgentPopup::fateChosen, this,
                [&](const Worktree &worktree, CloseAgentPopup::WorktreeFate fate) {
                    chosenName = worktree.name;
                    chosenFate = fate;
                });

        popup.keepButton()->click();

        QCOMPARE(chosenName, QString("agent-1"));
        QCOMPARE(chosenFate, CloseAgentPopup::WorktreeFate::Keep);
    }

    void closeAgentPopupAsksForEachWorktreeOnceInTurn()
    {
        QWidget window;
        CloseAgentPopup popup(Theme::dark(), &window);
        QStringList chosen;
        connect(&popup, &CloseAgentPopup::fateChosen, this,
                [&](const Worktree &worktree, CloseAgentPopup::WorktreeFate) { chosen.append(worktree.name); });

        popup.load({.name = "agent-1"});
        popup.load({.name = "agent-2"});
        popup.load({.name = "agent-1"}); // its tab closed again
        popup.load({.name = "agent-3"});

        QCOMPARE(popup.waitingCount(), 2);
        popup.keepButton()->click();
        QCOMPARE(popup.asked()->name, QString("agent-2"));
        popup.next(); // cancelled: agent-2's tab stays open
        popup.deleteButton()->click();
        QCOMPARE(chosen, QStringList({"agent-1", "agent-3"}));
        QVERIFY(!popup.asked());
    }

    void messagePopupShowsTheWaitingMessagesInTurn()
    {
        QWidget window;
        MessagePopup popup(QStringLiteral("claude"), QStringLiteral("testMessage"), Theme::dark(), &window);

        popup.load("Merge agent-1", "Conflicts");
        popup.load("Merge agent-1", "Conflicts"); // repeated
        popup.load("Merge agent-2", "Check failed");

        QCOMPARE(popup.text(), QString("Conflicts"));
        QCOMPARE(popup.waitingCount(), 1);
        popup.next();
        QCOMPARE(popup.text(), QString("Check failed"));
        popup.next();
        QVERIFY(popup.text().isEmpty());
    }

    void agentCheckPopupSavesTheTrimmedCommand()
    {
        QWidget window;
        AgentCheckPopup popup(Theme::dark(), &window);
        QSignalSpy saved(&popup, &AgentCheckPopup::saved);
        popup.commandField()->setText("  ctest --test-dir build ");

        popup.save();

        QCOMPARE(saved.count(), 1);
        QCOMPARE(saved.first().at(0).toString(), QString("ctest --test-dir build"));
    }

    void findsTheUserStatusLine()
    {
        const QTemporaryDir project;
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
        })")
                                       .object();
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
        const QSignalSpy attention(&activity, &ClaudeActivity::attentionRequested);
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
        const QSignalSpy attention(&activity, &ClaudeActivity::attentionRequested);
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

    // End to end: the real duet-hook binary, launched as Claude Code
    // would, relays the event to the editor's server.
    void hookProgramRelaysEventsToTheServer()
    {
        HookServer server;
        QVERIFY(server.listen());
        QSignalSpy received(&server, &HookServer::hookReceived);

        QProcess hook;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("DUET_SOCKET", server.serverName());
        hook.setProcessEnvironment(environment);
        hook.start(QStringLiteral(DUET_HOOK_PATH), {});
        QVERIFY(hook.waitForStarted());
        hook.write(R"({"hook_event_name":"Stop","session_id":"hello"})");
        hook.closeWriteChannel();

        QTRY_COMPARE(received.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(hook.state(), QProcess::NotRunning, 5000);
        QCOMPARE(hook.exitCode(), 0);
        QVERIFY(hook.readAllStandardOutput().isEmpty()); // nothing must end up in Claude's context
        QCOMPARE(received.first().first().value<HookEvent>().sessionId, QStringLiteral("hello"));
    }

    // A slot may postpone the reply (snapshot off the UI thread): the hook,
    // and so Claude, waits until it is sent.
    void hookWaitsForADeferredReply()
    {
        HookServer server;
        QVERIFY(server.listen());
        std::function<void()> reply;
        connect(&server, &HookServer::hookReceived, this, [&] { reply = server.deferReply(); });

        QProcess hook;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("DUET_SOCKET", server.serverName());
        hook.setProcessEnvironment(environment);
        hook.start(QStringLiteral(DUET_HOOK_PATH), {});
        QVERIFY(hook.waitForStarted());
        hook.write(R"({"hook_event_name":"Stop","session_id":"hello"})");
        hook.closeWriteChannel();

        QTRY_VERIFY(reply);
        QTest::qWait(300);
        QCOMPARE(hook.state(), QProcess::Running);
        reply();
        QTRY_COMPARE_WITH_TIMEOUT(hook.state(), QProcess::NotRunning, 5000);
        QCOMPARE(hook.exitCode(), 0);
    }

    // The editor's answer is what the hook prints: Claude Code reads it.
    void hookPrintsTheEditorsAnswer()
    {
        HookServer server;
        QVERIFY(server.listen());
        std::function<void()> reply;
        connect(&server, &HookServer::hookReceived, this, [&] {
            reply = server.deferReply();
            server.respond(EditorContext::promptHookOutput(QStringLiteral("Open file: a.cpp")));
        });

        QProcess hook;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("DUET_SOCKET", server.serverName());
        hook.setProcessEnvironment(environment);
        hook.start(QStringLiteral(DUET_HOOK_PATH), {});
        QVERIFY(hook.waitForStarted());
        hook.write(R"({"hook_event_name":"UserPromptSubmit","prompt":"fix this"})");
        hook.closeWriteChannel();

        QTRY_VERIFY(reply);
        reply();
        QTRY_COMPARE_WITH_TIMEOUT(hook.state(), QProcess::NotRunning, 5000);
        QCOMPARE(hook.exitCode(), 0);
        const QJsonObject output = QJsonDocument::fromJson(hook.readAllStandardOutput()).object();
        const QJsonObject specific = output.value("hookSpecificOutput").toObject();
        QCOMPARE(specific.value("hookEventName").toString(), QStringLiteral("UserPromptSubmit"));
        QCOMPARE(specific.value("additionalContext").toString(), QStringLiteral("Open file: a.cpp"));
    }

    void codeRequestNamesTheSelectedLines()
    {
        const QTemporaryDir project;
        QDir(project.path()).mkpath("src");
        const QString path = project.filePath("src/a.cpp");
        writeFile(path, "int a;\nint b;\nint c;\nint d;\n");
        EditorWidget editor(Theme::dark());
        QVERIFY(editor.load(path, nullptr));
        editor.setSelection(1, 0, 3, 0); // whole lines 2 and 3

        const std::optional<CodeRequest::Target> target = CodeRequest::targetIn(editor, project.path());

        QVERIFY(target);
        QCOMPARE(CodeRequest::mention(*target), QStringLiteral("@src/a.cpp#L2-3"));
        QCOMPARE(CodeRequest::prompt(CodeRequest::Kind::Review, *target),
                 QStringLiteral("Review @src/a.cpp#L2-3: point out problems with their line, then propose fixes, "
                                "applying nothing until I agree."));
    }

    void codeRequestWithoutSelectionNamesTheSymbol()
    {
        const QTemporaryDir project;
        const QString path = project.filePath("a.cpp");
        writeFile(path, "int parse();\n");
        EditorWidget editor(Theme::dark());
        QVERIFY(editor.load(path, nullptr));
        editor.setCursorPosition(0, 6);

        const std::optional<CodeRequest::Target> target = CodeRequest::targetIn(editor, project.path());

        QVERIFY(target);
        QCOMPARE(target->symbol, QStringLiteral("parse"));
        const QString prompt = CodeRequest::prompt(CodeRequest::Kind::Explain, *target);
        QVERIFY(prompt.startsWith(QStringLiteral("Explain `parse` at @a.cpp#L1: ")));
        QVERIFY(!prompt.contains(QStringLiteral("unsaved")));
    }

    void codeRequestWarnsOfUnsavedChanges()
    {
        const QTemporaryDir project;
        const QString path = project.filePath("a.cpp");
        writeFile(path, "int a;\n");
        EditorWidget editor(Theme::dark());
        QVERIFY(editor.load(path, nullptr));
        editor.insertAt(QStringLiteral("// new\n"), 0, 0);

        const std::optional<CodeRequest::Target> target = CodeRequest::targetIn(editor, project.path());

        QVERIFY(target && target->unsaved);
        QVERIFY(CodeRequest::prompt(CodeRequest::Kind::WriteTests, *target).contains(QStringLiteral("unsaved")));
    }

    void codeRequestMentionsQuotePathsWithSpaces()
    {
        const CodeRequest::Target target{.path = QStringLiteral("my dir/a.cpp"), .firstLine = 4, .lastLine = 4};
        QCOMPARE(CodeRequest::mention(target), QStringLiteral("@\"my dir/a.cpp\"#L4"));
    }

    void codeRequestNeedsAFile()
    {
        EditorWidget view(Theme::dark());
        view.showReadOnlyText(QStringLiteral("diff"), QString());
        QVERIFY(!CodeRequest::targetIn(view, QString()));
    }

    void describesTheEditorContext()
    {
        EditorContext context;
        QVERIFY(context.describe().isEmpty());

        context.filePath = QStringLiteral("src/a.cpp");
        context.line = 12;
        context.column = 5;
        context.selectionStartLine = 10;
        context.selectionEndLine = 12;
        context.selectedText = QStringLiteral("int a = 0;\n```\nreturn a;");
        context.problems.append({11, true, QStringLiteral("use of undeclared\n identifier 'b'")});
        context.problems.append({12, false, QStringLiteral("unused variable")});
        context.unsavedFiles = {QStringLiteral("src/a.cpp"), QStringLiteral("src/b.h")};

        const QString text = context.describe();
        QVERIFY(text.contains(QStringLiteral("- File open in the editor: src/a.cpp, cursor at line 12, column 5")));
        QVERIFY(
            text.contains(QStringLiteral("- Selected text (lines 10-12):\n````\nint a = 0;\n```\nreturn a;\n````")));
        QVERIFY(text.contains(QStringLiteral("  - line 11, error: use of undeclared identifier 'b'")));
        QVERIFY(text.contains(QStringLiteral("  - line 12, warning: unused variable")));
        QVERIFY(text.contains(
            QStringLiteral("- Unsaved changes in the editor (the files on disk are older): src/a.cpp, src/b.h")));
    }

    void cutsALongSelection()
    {
        EditorContext context;
        context.filePath = QStringLiteral("a.txt");
        context.line = context.column = context.selectionStartLine = context.selectionEndLine = 1;
        context.selectedText = QString(EditorContext::maximumSelectionLength + 100, QLatin1Char('x'));
        const QString text = context.describe();
        QVERIFY(text.contains(QStringLiteral("(line 1, cut)")));
        QVERIFY(!text.contains(QString(EditorContext::maximumSelectionLength + 1, QLatin1Char('x'))));
    }

    void statusLineIsForwardedAndPrinted()
    {
        HookServer server;
        QVERIFY(server.listen());
        QSignalSpy received(&server, &HookServer::hookReceived);

        QProcess hook;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("DUET_SOCKET", server.serverName());
        environment.remove("DUET_STATUSLINE_COMMAND");
        hook.setProcessEnvironment(environment);
        hook.start(QStringLiteral(DUET_HOOK_PATH), {"statusline"});
        QVERIFY(hook.waitForStarted());
        hook.write(R"({"model":{"display_name":"Opus"},"context_window":{"used_percentage":42}})");
        hook.closeWriteChannel();

        QTRY_COMPARE(received.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(hook.state(), QProcess::NotRunning, 5000);
        const auto event = received.first().first().value<HookEvent>();
        QCOMPARE(event.kind, HookEvent::Kind::StatusUpdated);
        QCOMPARE(event.status["model"].toObject()["display_name"].toString(), QStringLiteral("Opus"));
        QCOMPARE(hook.readAllStandardOutput().trimmed(), QByteArray("Opus · context 42%"));
    }

    void hookProgramNeverFailsWithoutEditor()
    {
        QProcess hook;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("DUET_SOCKET", "duet-nonexistent-server");
        hook.setProcessEnvironment(environment);
        hook.start(QStringLiteral(DUET_HOOK_PATH), {});
        QVERIFY(hook.waitForStarted());
        hook.write(R"({"hook_event_name":"Stop"})");
        hook.closeWriteChannel();
        QVERIFY(hook.waitForFinished(5000));
        QCOMPARE(hook.exitCode(), 0);
    }

    void listsTheConfigurationByCategoryAndScope()
    {
        const QTemporaryDir user;
        const QTemporaryDir project;
        QVERIFY(user.isValid() && project.isValid());
        qputenv("CLAUDE_CONFIG_DIR", user.path().toUtf8());
        writeFile(user.filePath("CLAUDE.md"), "# Mine\n");
        writeFile(user.filePath("skills/review/SKILL.md"),
                  "---\nname: review-cpp\ndescription: \"Reviews C++ code\"\n---\n\nBody\n");
        writeFile(user.filePath("agents/tester.md"), "---\nname: tester\ndescription: Runs the tests\n---\n");
        writeFile(user.filePath("commands/frontend/deploy.md"), "Deploy\n");
        writeFile(project.filePath("CLAUDE.md"), "# Project\n");
        writeFile(project.filePath("src/CLAUDE.md"), "# Sources\n");
        writeFile(project.filePath(".claude/rules/style.md"), "# Style\n");
        writeFile(project.filePath(".claude/settings.json"), "{}\n");
        writeFile(QDir(ClaudeConfig::autoMemoryDir(project.path())).filePath("MEMORY.md"), "- index\n");

        const QList<ClaudeConfig::Entry> entries = ClaudeConfig::entries(project.path(), {});
        qunsetenv("CLAUDE_CONFIG_DIR");
        const auto find = [&](ClaudeConfig::Category category, const QString &name) -> const ClaudeConfig::Entry * {
            for (const ClaudeConfig::Entry &entry : entries) {
                if (entry.category == category && entry.name == name)
                    return &entry;
            }
            return nullptr;
        };
        using C = ClaudeConfig::Category;
        using S = ClaudeConfig::Scope;
        const ClaudeConfig::Entry *skill = find(C::Skills, "review-cpp");
        QVERIFY(skill);
        QCOMPARE(skill->scope, S::User);
        QCOMPARE(skill->description, QStringLiteral("Reviews C++ code"));
        QCOMPARE(skill->path, QDir::cleanPath(user.filePath("skills/review/SKILL.md")));
        QVERIFY(find(C::Agents, "tester"));
        QVERIFY(find(C::Commands, "/frontend/deploy"));
        QCOMPARE(find(C::Rules, "style.md")->scope, S::Project);
        QCOMPARE(find(C::Memory, "src/CLAUDE.md")->scope, S::Subfolder);
        QCOMPARE(find(C::Memory, "MEMORY.md")->scope, S::Auto);
        QVERIFY(find(C::Settings, ".claude/settings.json")->exists);

        // Usual files appear before they exist.
        const ClaudeConfig::Entry *local = find(C::Memory, "CLAUDE.local.md");
        QVERIFY(local);
        QVERIFY(!local->exists);
        QVERIFY(!find(C::Settings, ".claude/settings.local.json")->exists);
        QVERIFY(!find(C::Mcp, ".mcp.json")->exists);

        // By category, then from the lowest precedence to the highest.
        for (qsizetype i = 1; i < entries.size(); ++i) {
            QVERIFY(entries[i - 1].category <= entries[i].category);
            if (entries[i - 1].category == entries[i].category)
                QVERIFY(entries[i - 1].scope <= entries[i].scope);
        }
    }

    void sessionSettingsAreReadOnly()
    {
        const QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath("claude-hooks.json"), "{}\n");
        qputenv("CLAUDE_CONFIG_DIR", dir.filePath("config").toUtf8());
        const QList<ClaudeConfig::Entry> entries = ClaudeConfig::entries(dir.path(), dir.filePath("claude-hooks.json"));
        qunsetenv("CLAUDE_CONFIG_DIR");
        const auto session = std::find_if(entries.begin(), entries.end(), [](const ClaudeConfig::Entry &entry) {
            return entry.scope == ClaudeConfig::Scope::Session;
        });
        QVERIFY(session != entries.end());
        QVERIFY(session->readOnly);
        QCOMPARE(session->category, ClaudeConfig::Category::Settings);
    }

    void autoMemoryFollowsClaudeCodeNaming()
    {
        qputenv("CLAUDE_CONFIG_DIR", "/c");
        QCOMPARE(ClaudeConfig::autoMemoryDir("/home/me/my_app.v2"),
                 QStringLiteral("/c/projects/-home-me-my-app-v2/memory"));
        qunsetenv("CLAUDE_CONFIG_DIR");
    }

    void readsFrontMatter()
    {
        const QMap<QString, QString> fields =
            ClaudeConfig::frontMatter("---\nname: x\ndescription: 'Does: things'\ntools:\n  - Read\n---\nname: body\n");
        QCOMPARE(fields.value("name"), QStringLiteral("x"));
        QCOMPARE(fields.value("description"), QStringLiteral("Does: things"));
        QVERIFY(!fields.contains("  - Read"));
        QVERIFY(ClaudeConfig::frontMatter("# Title\nname: x\n").isEmpty());
    }

    void viewCreatesItemsAndUsualFiles()
    {
        const QTemporaryDir user;
        const QTemporaryDir project;
        QVERIFY(user.isValid() && project.isValid());
        qputenv("CLAUDE_CONFIG_DIR", user.path().toUtf8());
        ClaudeConfigView view;
        view.show();
        view.setProjectFolder(project.path());
        QSignalSpy opened(&view, &ClaudeConfigView::fileRequested);

        QVERIFY(!view.createNamed(ClaudeConfig::Category::Skills, ClaudeConfig::Scope::Project, "Bad Name").isEmpty());
        QCOMPARE(view.createNamed(ClaudeConfig::Category::Skills, ClaudeConfig::Scope::Project, "release-notes"),
                 QString());
        const QString skill = project.filePath(".claude/skills/release-notes/SKILL.md");
        QVERIFY(QFileInfo::exists(skill));
        QCOMPARE(opened.count(), 1);
        QCOMPARE(opened.last().at(0).toString(), skill);
        QCOMPARE(ClaudeConfig::frontMatter(readFile(skill)).value("name"), QStringLiteral("release-notes"));
        QVERIFY(!view.createNamed(ClaudeConfig::Category::Skills, ClaudeConfig::Scope::Project, "release-notes")
                     .isEmpty()); // already there

        // A double-click on CLAUDE.local.md, not created yet, creates it.
        QTreeWidgetItem *local = nullptr;
        for (QTreeWidgetItemIterator it(view.tree()); *it; ++it) {
            if (const ClaudeConfig::Entry *entry = view.entryOf(*it); entry && entry->name == "CLAUDE.local.md")
                local = *it;
        }
        QVERIFY(local);
        QVERIFY(!view.entryOf(local)->exists);
        view.activate(local);
        QVERIFY(QFileInfo::exists(project.filePath("CLAUDE.local.md")));
        QCOMPARE(opened.last().at(0).toString(), QDir::cleanPath(project.filePath("CLAUDE.local.md")));
        qunsetenv("CLAUDE_CONFIG_DIR");
    }

    void viewDeletesItems()
    {
        const QTemporaryDir user;
        const QTemporaryDir project;
        QVERIFY(user.isValid() && project.isValid());
        qputenv("CLAUDE_CONFIG_DIR", user.path().toUtf8());
        ClaudeConfigView view;
        view.show();
        view.setProjectFolder(project.path());
        QCOMPARE(view.createNamed(ClaudeConfig::Category::Skills, ClaudeConfig::Scope::Project, "doomed"), QString());
        QCOMPARE(view.createNamed(ClaudeConfig::Category::Agents, ClaudeConfig::Scope::Project, "gone"), QString());
        const auto find = [&view](const QString &name) -> const ClaudeConfig::Entry * {
            for (QTreeWidgetItemIterator it(view.tree()); *it; ++it) {
                if (const ClaudeConfig::Entry *entry = view.entryOf(*it); entry && entry->name == name)
                    return entry;
            }
            return nullptr;
        };
        const QSignalSpy deleted(&view, &ClaudeConfigView::entryDeleted);

        // A skill goes with its folder.
        const QString skillDir = project.filePath(".claude/skills/doomed");
        QVERIFY(find("doomed"));
        QCOMPARE(ClaudeConfigView::deletionTarget(*find("doomed")), skillDir);
        QCOMPARE(view.removeTarget(skillDir, true), QString());
        QVERIFY(!QFileInfo::exists(skillDir));
        QVERIFY(!find("doomed"));
        QCOMPARE(deleted.count(), 1);

        const QString agent = project.filePath(".claude/agents/gone.md");
        QCOMPARE(ClaudeConfigView::deletionTarget(*find("gone")), agent);
        QCOMPARE(view.removeTarget(agent, true), QString());
        QVERIFY(!QFileInfo::exists(agent));
        QVERIFY(!view.removeTarget(agent, true).isEmpty());

        // Nothing to delete in a file not created yet, or a read-only one.
        QVERIFY(find("CLAUDE.local.md"));
        QVERIFY(ClaudeConfigView::deletionTarget(*find("CLAUDE.local.md")).isEmpty());
        ClaudeConfig::Entry readOnly;
        readOnly.path = project.filePath("x.json");
        readOnly.readOnly = true;
        QVERIFY(ClaudeConfigView::deletionTarget(readOnly).isEmpty());
        qunsetenv("CLAUDE_CONFIG_DIR");
    }

    void viewShowsProjectOrUserConfiguration()
    {
        const QTemporaryDir user;
        const QTemporaryDir project;
        QVERIFY(user.isValid() && project.isValid());
        qputenv("CLAUDE_CONFIG_DIR", user.path().toUtf8());
        ClaudeConfigView view;
        view.show();
        view.setProjectFolder(project.path());
        const auto shownScopes = [&view] {
            QSet<ClaudeConfig::Scope> scopes;
            for (QTreeWidgetItemIterator it(view.tree()); *it; ++it) {
                if (const ClaudeConfig::Entry *entry = view.entryOf(*it))
                    scopes.insert(entry->scope);
            }
            return scopes;
        };

        QCOMPARE(view.level(), ClaudeConfigView::Level::Project);
        QVERIFY(shownScopes().contains(ClaudeConfig::Scope::Project));
        QVERIFY(!shownScopes().contains(ClaudeConfig::Scope::User));

        view.setLevel(ClaudeConfigView::Level::User);
        QVERIFY(shownScopes().contains(ClaudeConfig::Scope::User));
        QVERIFY(!shownScopes().contains(ClaudeConfig::Scope::Project));

        // A new item in the other level switches to it.
        QCOMPARE(view.createNamed(ClaudeConfig::Category::Agents, ClaudeConfig::Scope::Project, "reviewer"), QString());
        QCOMPARE(view.level(), ClaudeConfigView::Level::Project);
        qunsetenv("CLAUDE_CONFIG_DIR");
    }

    void mergesSettingsByPrecedence()
    {
        const QTemporaryDir user;
        const QTemporaryDir project;
        QVERIFY(user.isValid() && project.isValid());
        qputenv("CLAUDE_CONFIG_DIR", user.path().toUtf8());
        writeFile(
            user.filePath("settings.json"),
            R"j({"model":"sonnet","permissions":{"allow":["Bash(make)"],"defaultMode":"default"},"env":{"A":"1"}})j");
        writeFile(project.filePath(".claude/settings.json"),
                  R"j({"permissions":{"allow":["Bash(make)","Read"]},"env":{"B":"2"}})j");
        writeFile(project.filePath(".claude/settings.local.json"), R"j({"$schema":"x","model":"opus"})j");
        writeFile(project.filePath("session.json"), "not json");
        const QList<EffectiveSettings::Layer> layers =
            EffectiveSettings::read(project.path(), project.filePath("session.json"));
        qunsetenv("CLAUDE_CONFIG_DIR");

        QCOMPARE(layers.size(), 5);
        QCOMPARE(layers.first().scope, ClaudeConfig::Scope::User);
        QCOMPARE(layers.last().scope, ClaudeConfig::Scope::Managed);
        QVERIFY(!layers[3].error.isEmpty()); // the session file, ignored

        const QJsonObject merged = EffectiveSettings::merged(layers);
        QCOMPARE(merged.value("model").toString(), QStringLiteral("opus"));
        QVERIFY(!merged.contains("$schema"));
        QCOMPARE(merged.value("permissions").toObject().value("allow").toArray(), (QJsonArray{"Bash(make)", "Read"}));
        QCOMPARE(merged.value("permissions").toObject().value("defaultMode").toString(), QStringLiteral("default"));
        QCOMPARE(merged.value("env").toObject(), (QJsonObject{{"A", "1"}, {"B", "2"}}));

        const QString text = EffectiveSettings::render(layers, project.path());
        const auto lineWith = [&](const QString &needle) {
            for (const QString &line : text.split('\n')) {
                if (line.contains(needle))
                    return line;
            }
            return QString();
        };
        QVERIFY(lineWith("\"model\": \"opus\"").contains("// Local, over User: \"sonnet\""));
        QVERIFY(lineWith("\"Read\"").contains("// Project"));
        QVERIFY(lineWith("\"Bash(make)\"").endsWith("\"Bash(make)\",")); // as the array: User
        QVERIFY(lineWith("\"permissions\": {").contains("// User"));
        QVERIFY(lineWith("\"B\": \"2\"").contains("// Project"));
        QVERIFY(lineWith("session.json").contains("ignored"));
    }

    void lintRecognizesClaudeFiles()
    {
        qputenv("CLAUDE_CONFIG_DIR", "/home/me/.claude");
        using K = ClaudeLint::Kind;
        QCOMPARE(ClaudeLint::kindOf("/home/me/.claude/settings.json"), K::Settings);
        QCOMPARE(ClaudeLint::kindOf("/p/.claude/settings.local.json"), K::Settings);
        QCOMPARE(ClaudeLint::kindOf("/etc/claude-code/managed-settings.json"), K::Settings);
        QCOMPARE(ClaudeLint::kindOf("/p/settings.json"), K::None);
        QCOMPARE(ClaudeLint::kindOf("/p/.claude/skills/review/SKILL.md"), K::Skill);
        QCOMPARE(ClaudeLint::kindOf("/p/.claude/agents/tester.md"), K::Agent);
        QCOMPARE(ClaudeLint::kindOf("/p/docs/agents/tester.md"), K::None);
        qunsetenv("CLAUDE_CONFIG_DIR");
    }

    void lintChecksSettingsAgainstTheSchema()
    {
        const QByteArray text = "{\n  \"model\": \"opus\",\n  \"permissions\": {\"defaultMode\": \"sometimes\"},\n"
                                "  \"cleanupPeriodDays\": \"7\"\n}\n";
        const Diagnostics diagnostics = ClaudeLint::check(ClaudeLint::Kind::Settings, text);
        QStringList messages;
        for (const Diagnostic &diagnostic : diagnostics)
            messages.append(diagnostic.message);
        QVERIFY2(diagnostics.size() == 2, qPrintable(messages.join(" | ")));
        QCOMPARE(diagnostics[0].start, int(text.indexOf("\"sometimes\"")));
        QVERIFY(messages[0].contains("\"acceptEdits\""));
        QCOMPARE(diagnostics[1].start, int(text.indexOf("\"7\"")));

        const Diagnostics syntax = ClaudeLint::check(ClaudeLint::Kind::Settings, "{\"model\": \"opus\",}");
        QCOMPARE(syntax.size(), 1);
        QCOMPARE(syntax[0].severity, Diagnostic::Severity::Error);
        QCOMPARE(syntax[0].start, 17);

        const Diagnostics rules = ClaudeLint::check(
            ClaudeLint::Kind::Settings, "{\"permissions\": {\"allow\": [\"Bash(make)\", \"Wirte\", \"Read(\"]}}");
        QCOMPARE(rules.size(), 2);
        QVERIFY(rules[0].message.contains("did you mean “Write”"));
        QVERIFY(rules[1].message.contains("Tool permission rule")); // the pattern is too long to show

        QVERIFY(ClaudeLint::settingsHelp(text, int(text.indexOf("model")) + 1).startsWith("model\n"));
        QVERIFY(ClaudeLint::settingsHelp(text, 0).isEmpty());
    }

    void lintChecksFrontMatter()
    {
        using K = ClaudeLint::Kind;
        QVERIFY(ClaudeLint::check(K::Skill, "---\nname: review\ndescription: Reviews code\n---\nBody\n").isEmpty());
        QCOMPARE(ClaudeLint::check(K::Skill, "# Review\n").size(), 1);
        QCOMPARE(ClaudeLint::check(K::Skill, "---\nname: review\n").first().severity, Diagnostic::Severity::Error);

        const QByteArray bad = "---\nname: Review Code\n---\n";
        const Diagnostics skill = ClaudeLint::check(K::Skill, bad);
        QCOMPARE(skill.size(), 2); // the name, the missing description
        QCOMPARE(skill[0].start, int(bad.indexOf("Review Code")));
        QCOMPARE(skill[0].end, int(bad.indexOf("\n---\n", 4)));
        QVERIFY(skill[1].message.contains("description"));

        const Diagnostics agent = ClaudeLint::check(K::Agent, "---\ndescription: Runs tests\n---\n");
        QCOMPARE(agent.size(), 1);
        QVERIFY(agent[0].message.contains("name"));
    }

    void tidiesTheCommitMessageClaudeHandsBack()
    {
        QCOMPARE(CommitMessage::tidy("Add a\n\nBody\n"), QStringLiteral("Add a\n\nBody"));
        QCOMPARE(CommitMessage::tidy("```text\nAdd a\n```"), QStringLiteral("Add a"));
        QCOMPARE(CommitMessage::tidy("\"Add a\""), QStringLiteral("Add a"));
        QVERIFY(CommitMessage::tidy("  ").isEmpty());

        // The body is wrapped at 72 columns, a list item under its text.
        const QString word = QStringLiteral("abcdefghi"); // 10 characters with its space
        const QString summary = QStringList(8, word).join(' ') + " longer";
        const QString message = CommitMessage::tidy(summary + "\n\n" + QStringList(10, word).join(' ') + "\n- " +
                                                    QStringList(8, word).join(' '));
        const QString seven = QStringList(7, word).join(' ');
        QCOMPARE(message.split('\n'),
                 QStringList({summary, "", seven, QStringList(3, word).join(' '), "- " + seven, "  " + word}));
    }

    void commitMessageRequestSaysWhatTheCommitHolds()
    {
        using CommitMessage::Scope;
        QVERIFY(CommitMessage::request(Scope::Staged, {}).contains("git diff --cached)"));
        QVERIFY(CommitMessage::request(Scope::Amend, {}).contains("git diff --cached HEAD^"));
        QVERIFY(CommitMessage::request(Scope::Everything, "src").contains("git diff HEAD -- src"));
        QVERIFY(CommitMessage::request(Scope::Everything, {}).contains("git diff HEAD -- ."));
        QVERIFY(!CommitMessage::request(Scope::Staged, {}).contains('\n'));
    }

private:
    static void writeFile(const QString &path, const QByteArray &content)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    }

    static QByteArray readFile(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
};

QTEST_MAIN(TestClaude)
#include "tst_claude.moc"
