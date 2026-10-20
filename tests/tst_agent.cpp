// SPDX-License-Identifier: GPL-3.0-only

#include "agent/AgentActivity.h"
#include "agent/AgentIndicator.h"
#include "agent/AgentProviders.h"
#include "agent/HookServer.h"
#include "agent/vendor/claude/ClaudeCodeProvider.h"

#include <QLocalSocket>
#include <QSignalSpy>
#include <QtTest>

class TestAgent : public QObject {
    Q_OBJECT

private slots:

    void activityNamesTheAgentInItsMessages()
    {
        AgentActivity activity;
        activity.setAgentName(QStringLiteral("Codex"));
        const QSignalSpy attention(&activity, &AgentActivity::attentionRequested);
        HookEvent event{};

        event.kind = HookEvent::Kind::PromptSubmitted;
        activity.handle(event);
        QCOMPARE(activity.description(), QStringLiteral("Codex is working"));

        event.kind = HookEvent::Kind::ResponseFinished;
        activity.handle(event);
        QCOMPARE(attention.size(), 1);
        QCOMPARE(attention.first().first().toString(), QStringLiteral("Codex has finished"));
    }

    void providersAreFoundByTheirKey()
    {
        for (const AgentProvider *provider : AgentProviders::all())
            QCOMPARE(&AgentProviders::find(AgentProviders::key(*provider)), provider);
        QCOMPARE(AgentProviders::key(AgentProviders::find(QStringLiteral("claude-code"))),
                 QStringLiteral("claude-code"));
    }

    void unknownProviderKeyFallsBackToClaudeCode()
    {
        QCOMPARE(AgentProviders::find(QStringLiteral("gone")).id(), AgentProviderId::ClaudeCode);
        QCOMPARE(AgentProviders::find(QString()).id(), AgentProviderId::ClaudeCode);
    }

    void claudeProviderNamesItselfClaude()
    {
        const ClaudeCodeProvider claude;
        QCOMPARE(claude.name(), QStringLiteral("Claude"));
        QCOMPARE(claude.iconName(), QStringLiteral("claude"));
        QVERIFY(claude.features().hasAgentMessaging);
    }

    void claudeRepliesInItsHookLanguage()
    {
        const ClaudeCodeProvider claude;
        const QJsonObject context = claude.promptContextReply(QStringLiteral("a.cpp"))["hookSpecificOutput"].toObject();
        QCOMPARE(context["hookEventName"].toString(), QStringLiteral("UserPromptSubmit"));
        QCOMPARE(context["additionalContext"].toString(), QStringLiteral("a.cpp"));

        const QJsonObject block = claude.blockReply(QStringLiteral("fix it"));
        QCOMPARE(block["decision"].toString(), QStringLiteral("block"));
        QCOMPARE(block["reason"].toString(), QStringLiteral("fix it"));
    }

    void claudeMarkTwinklesOnceThenStaysStill()
    {
        const ClaudeCodeProvider claude;
        const AgentMark &mark = claude.mark();
        QVERIFY(!mark.frameStep(AgentStatus::Ready));
        QVERIFY(mark.frameStep(AgentStatus::Working));
        QVERIFY(!mark.isFinished({.status = AgentStatus::Done, .index = 0}));
        QVERIFY(mark.isFinished({.status = AgentStatus::Done, .index = mark.loopFrames() - 1}));
        QVERIFY(!mark.isFinished({.status = AgentStatus::Working, .index = mark.loopFrames() - 1}));
    }

    void indicatorIsNamedAfterItsAgent()
    {
        const ClaudeCodeProvider claude;
        AgentIndicator indicator;

        indicator.setProvider(&claude);
        indicator.setStatus(AgentStatus::Working);

        QCOMPARE(indicator.accessibleName(), QStringLiteral("Claude: Working"));
    }

    void hookServerWithoutProviderRepliesButEmitsNothing()
    {
        HookServer server;
        QVERIFY(server.listen());
        const QSignalSpy received(&server, &HookServer::hookReceived);

        QLocalSocket socket;
        socket.connectToServer(server.serverName());
        QVERIFY(socket.waitForConnected());
        socket.write(R"({"hook_event_name":"Stop","session_id":"s1"})"
                     "\n");
        QTRY_VERIFY(socket.canReadLine());

        QCOMPARE(socket.readLine(), QByteArray("ok\n"));
        QCOMPARE(received.size(), 0);
    }
};

QTEST_MAIN(TestAgent)
#include "tst_agent.moc"
