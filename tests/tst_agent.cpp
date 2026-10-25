// SPDX-License-Identifier: GPL-3.0-only

#include "agent/AgentActivity.h"
#include "agent/AgentIndicator.h"
#include "agent/AgentProviders.h"
#include "agent/HookServer.h"
#include "agent/ProjectCheck.h"
#include "agent/ReviewBar.h"
#include "agent/ReviewFindingsPanel.h"
#include "agent/ReviewPage.h"
#include "agent/ReviewSession.h"
#include "agent/vendor/claude/ClaudeCodeProvider.h"
#include "support/FakeReviewer.h"

#include <QLabel>
#include <QLocalSocket>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// The texts of the labels of `widget` shown.
QStringList shownTexts(const QWidget &widget)
{
    QStringList texts;
    for (const QLabel *label : widget.findChildren<QLabel *>()) {
        if (label->isVisible())
            texts.append(label->text());
    }
    return texts;
}

} // namespace

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

    void projectCheckReportsItsExitCodeAndOutput()
    {
        const QTemporaryDir folder;
        ProjectCheck check;
        const QSignalSpy finished(&check, &ProjectCheck::finished);

        // The same line for cmd.exe and /bin/sh.
        check.start(QStringLiteral("echo checked && exit 3"), folder.path());

        QTRY_COMPARE(finished.size(), 1);
        const auto result = finished.first().first().value<ProjectCheck::Result>();
        QCOMPARE(result.exitCode, 3);
        QVERIFY(!result.hasPassed());
        QCOMPARE(result.lastLines(1), QStringLiteral("checked"));
    }

    void reviewAnywayGoesOnTimingFromTheCheck()
    {
        const QTemporaryDir folder;
        const FakeReviewer agent(FakeReviewer::oneFindingReport);
        ReviewSession session({.workDir = folder.path(),
                               .subject = "the code",
                               .checkCommand = QStringLiteral("echo checked && exit 3"),
                               .agent = &agent});
        session.start();
        QTRY_COMPARE(session.state(), ReviewSession::State::CheckFailed);
        const std::chrono::milliseconds checkTime = session.elapsed();
        QVERIFY(checkTime.count() > 0);

        session.reviewAnyway();

        QVERIFY(session.elapsed() >= checkTime);
        QTRY_COMPARE(session.state(), ReviewSession::State::Finished);
        QVERIFY(session.elapsed() >= checkTime);
    }

    void aReviewKeepsItsSummaryOnceTheLastFindingIsDismissed()
    {
        const QTemporaryDir folder;
        const FakeReviewer agent(FakeReviewer::oneFindingReport);
        ReviewSession session({.workDir = folder.path(), .subject = "the code", .agent = &agent});
        ReviewBar bar;
        ReviewFindingsPanel panel;
        bar.setSession(&session);
        panel.setSession(&session);
        bar.show();
        panel.show();
        session.start();
        QTRY_COMPARE(session.state(), ReviewSession::State::Finished);
        QCOMPARE(session.findings().size(), 1);

        session.dismiss({session.findings().first()});

        QVERIFY(shownTexts(panel).contains("Mostly sound."));
        const QString shown = (shownTexts(bar) + shownTexts(panel)).join('\n');
        QVERIFY(!shown.contains("No problems found."));
    }

    void reviewPageInvitesToAReviewThenShowsItsFindings()
    {
        const QTemporaryDir folder;
        const FakeReviewer agent(FakeReviewer::oneFindingReport);
        ReviewSession session({.workDir = folder.path(), .subject = "the code", .agent = &agent});
        ReviewPage page;
        page.show();
        QPushButton *start = nullptr;
        for (QPushButton *button : page.findChildren<QPushButton *>()) {
            if (button->text() == "Review the Staged Changes")
                start = button;
        }
        const auto *findings = page.findChild<ReviewFindingsPanel *>();
        QVERIFY(start && start->isVisible());
        QVERIFY(!findings->isVisible());
        const QSignalSpy requested(&page, &ReviewPage::reviewRequested);
        start->click();
        QCOMPARE(requested.size(), 1);

        page.setSession(&session);
        session.start();

        QVERIFY(!start->isVisible());
        QTRY_VERIFY(findings->isVisible());
        page.setSession(nullptr);
        QVERIFY(start->isVisible());
        QVERIFY(!findings->isVisible());
    }

    void projectCheckKeepsTheLastLinesOfItsOutput()
    {
        const ProjectCheck::Result result{.exitCode = 0, .output = QStringLiteral("a\nb\nc\n")};

        QVERIFY(result.hasPassed());
        QCOMPARE(result.lastLines(2), QStringLiteral("b\nc"));
        QCOMPARE(result.lastLines(10), QStringLiteral("a\nb\nc"));
    }
};

QTEST_MAIN(TestAgent)
#include "tst_agent.moc"
