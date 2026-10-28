// SPDX-License-Identifier: GPL-3.0-only

#include "agent/AgentActivity.h"
#include "agent/AgentIndicator.h"
#include "agent/AgentProviders.h"
#include "agent/HookServer.h"
#include "agent/PermissionPrompts.h"
#include "agent/ProjectCheck.h"
#include "agent/ReviewBar.h"
#include "agent/ReviewFileHeader.h"
#include "agent/ReviewFindingCard.h"
#include "agent/ReviewFindingsList.h"
#include "agent/ReviewFindingsPanel.h"
#include "agent/ReviewFindingsView.h"
#include "agent/ReviewPage.h"
#include "agent/ReviewProgressView.h"
#include "agent/ReviewSession.h"
#include "agent/ReviewSketch.h"
#include "agent/ReviewView.h"
#include "agent/vendor/claude/ClaudeCodeProvider.h"
#include "support/FakeReviewer.h"
#include "ui/Snackbar.h"
#include "ui/StepTimeline.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLocalSocket>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

#include <memory>

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

// A finished review of three findings, an error and a warning in a.cpp and
// a warning in b.cpp, shown in a list of findings.
struct ReviewedFindings {
    static inline const QString report = QStringLiteral(
        R"({"summary": "", "findings": [)"
        R"({"file": "a.cpp", "line": 3, "severity": "error", "problem": "Off by one.", "scenario": ""},)"
        R"({"file": "a.cpp", "line": 5, "severity": "warning", "problem": "Unused value.", "scenario": ""},)"
        R"({"file": "b.cpp", "line": 2, "severity": "warning", "problem": "Leaks the file.", "scenario": ""}]})");

    QTemporaryDir folder;
    FakeReviewer agent{report};
    ReviewSession session{{.workDir = folder.path(), .subject = "the code", .agent = &agent}};
    ReviewFindingsList list;

    ReviewedFindings()
    {
        list.setSession(&session);
        list.show();
        session.start();
        QTRY_COMPARE(session.state(), ReviewSession::State::Finished);
    }
};

// A hook's connection to `server`, as duet-hook makes it.
struct HookClient {
    QLocalSocket socket;

    explicit HookClient(const HookServer &server) { socket.connectToServer(server.serverName()); }

    void send(const QByteArray &json) { socket.write(json + '\n'); }

    // The JSON the editor answered, empty for a bare "ok".
    QJsonObject reply()
    {
        const QByteArray line = socket.readLine().trimmed();
        return QJsonDocument::fromJson(line.mid(3)).object();
    }
};

constexpr auto bashRequest =
    R"({"hook_event_name":"PermissionRequest","session_id":"s1","tool_name":"Bash","tool_input":)"
    R"({"command":"make test"}})";

// A window with a snackbar, its permission prompts, and a hook server
// that hands them every hook, as MainWindow does.
struct PermissionFixture {
    const ClaudeCodeProvider claude;
    QWidget window;
    HookServer server;
    Snackbar *snackbar = new Snackbar(&window);
    PermissionPrompts prompts{snackbar, &server, nullptr};

    PermissionFixture()
    {
        window.resize(800, 600);
        window.show();
        server.setProvider(&claude);
        prompts.setProvider(&claude);
        QObject::connect(&server, &HookServer::hookReceived, &prompts,
                         [this](const HookEvent &event) { prompts.handle(event, claude.name()); });
        server.listen();
    }

    QPushButton *button(const QString &text) const
    {
        for (QPushButton *button : snackbar->findChildren<QPushButton *>()) {
            if (button->text() == text)
                return button;
        }
        return nullptr;
    }
};

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

        const QJsonObject edit = claude.editContextReply(QStringLiteral("errors"))["hookSpecificOutput"].toObject();
        QCOMPARE(edit["hookEventName"].toString(), QStringLiteral("PostToolUse"));
        QCOMPARE(edit["additionalContext"].toString(), QStringLiteral("errors"));
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

    void aPermissionRequestWaitsInTheSnackbarForTheUsersAnswer()
    {
        PermissionFixture fixture;
        HookClient hook(fixture.server);

        hook.send(bashRequest);
        QTRY_COMPARE(fixture.prompts.pendingCount(), 1);

        QCOMPARE(fixture.snackbar->shownTitle(), QStringLiteral("Claude asks to use Bash"));
        QVERIFY(!hook.socket.canReadLine());
    }

    void allowingInTheSnackbarAnswersTheHook()
    {
        PermissionFixture fixture;
        HookClient hook(fixture.server);
        hook.send(bashRequest);
        QTRY_COMPARE(fixture.prompts.pendingCount(), 1);

        fixture.button(QStringLiteral("Allow"))->click();

        QTRY_VERIFY(hook.socket.canReadLine());
        const QJsonObject decision = hook.reply()["hookSpecificOutput"].toObject()["decision"].toObject();
        QCOMPARE(decision["behavior"].toString(), QStringLiteral("allow"));
        QCOMPARE(fixture.snackbar->count(), 0);
    }

    void closingTheSnackbarLeavesTheQuestionToTheTerminal()
    {
        PermissionFixture fixture;
        HookClient hook(fixture.server);
        hook.send(bashRequest);
        QTRY_COMPARE(fixture.prompts.pendingCount(), 1);
        auto *close = fixture.snackbar->findChild<QToolButton *>();

        close->click();

        QTRY_VERIFY(hook.socket.canReadLine());
        QVERIFY(hook.reply().isEmpty());
        QCOMPARE(fixture.prompts.pendingCount(), 0);
    }

    void aHookThatGivesUpTakesItsRequestAway()
    {
        PermissionFixture fixture;
        auto hook = std::make_unique<HookClient>(fixture.server);
        hook->send(bashRequest);
        QTRY_COMPARE(fixture.prompts.pendingCount(), 1);

        hook->socket.disconnectFromServer();

        QTRY_COMPARE(fixture.prompts.pendingCount(), 0);
        QCOMPARE(fixture.snackbar->count(), 0);
    }

    void aSessionThatMovesOnLeavesItsRequestToTheTerminal()
    {
        PermissionFixture fixture;
        HookClient request(fixture.server);
        request.send(bashRequest);
        QTRY_COMPARE(fixture.prompts.pendingCount(), 1);

        HookClient stop(fixture.server);
        stop.send(R"({"hook_event_name":"Stop","session_id":"s1"})");

        QTRY_VERIFY(request.socket.canReadLine());
        QVERIFY(request.reply().isEmpty());
        QCOMPARE(fixture.snackbar->count(), 0);
    }

    void requestsWaitTheirTurnInTheSnackbar()
    {
        PermissionFixture fixture;
        HookClient first(fixture.server);
        HookClient second(fixture.server);

        first.send(bashRequest);
        second.send(R"({"hook_event_name":"PermissionRequest","session_id":"s2","tool_name":"WebFetch","tool_input":)"
                    R"({"url":"example.com"}})");
        QTRY_COMPARE(fixture.prompts.pendingCount(), 2);
        fixture.button(QStringLiteral("Deny"))->click();

        QTRY_VERIFY(first.socket.canReadLine());
        QCOMPARE(first.reply()["hookSpecificOutput"].toObject()["decision"].toObject()["behavior"].toString(),
                 QStringLiteral("deny"));
        QCOMPARE(fixture.snackbar->shownTitle(), QStringLiteral("Claude asks to use WebFetch"));
    }

    void alwaysAllowIsOfferedOnlyWithRulesToRemember()
    {
        PermissionFixture fixture;
        HookClient hook(fixture.server);
        hook.send(bashRequest);
        QTRY_COMPARE(fixture.prompts.pendingCount(), 1);
        QVERIFY(!fixture.button(QStringLiteral("Always Allow")));
        fixture.button(QStringLiteral("Allow"))->click();
        QTRY_VERIFY(hook.socket.canReadLine());

        HookClient suggested(fixture.server);
        suggested.send(R"({"hook_event_name":"PermissionRequest","session_id":"s1","tool_name":"Bash","tool_input":)"
                       R"({"command":"make"},"permission_suggestions":[{"type":"addRules"}]})");
        QTRY_COMPARE(fixture.prompts.pendingCount(), 1);

        QVERIFY(fixture.button(QStringLiteral("Always Allow")));
    }

    void aSnackbarMessageIsAsTallAsItsTextsNeed()
    {
        QWidget window;
        window.resize(800, 600);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *snackbar = new Snackbar(&window);
        const Snackbar::Id shortMessage = snackbar->post({.title = QStringLiteral("Saved")});
        snackbar->withdraw(shortMessage);

        snackbar->post({
            .title = QStringLiteral("Claude asks to stage %1").arg(QStringLiteral("src/ui/Theme.cpp ").repeated(12)),
            .detail = QStringLiteral("first line\nsecond line\nthird line"),
            .actions = {{QStringLiteral("Deny"), {}}, {QStringLiteral("Allow"), {}}},
        });

        QCOMPARE(snackbar->height(), snackbar->heightForWidth(snackbar->width()));
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

    void reviewTabShowsItsProgressWhileTheReviewRunsThenItsFindings()
    {
        const QTemporaryDir folder;
        const FakeReviewer agent(FakeReviewer::oneFindingReport);
        ReviewView view({.workDir = folder.path(), .subject = "the code", .agent = &agent});
        view.show();
        const auto *progress = view.findChild<ReviewProgressView *>();
        const auto *sketch = view.findChild<ReviewSketch *>();
        const auto *steps = view.findChild<StepTimeline *>();

        view.session()->start();

        QVERIFY(progress->isVisible() && sketch->isVisible() && steps->isVisible());
        QVERIFY(shownTexts(*progress).join('\n').contains("is reviewing the code"));
        QVERIFY(steps->findChild<QTimer *>()->isActive());
        QTRY_COMPARE(view.session()->state(), ReviewSession::State::Finished);
        QVERIFY(!progress->isVisible());
        QVERIFY(!steps->findChild<QTimer *>()->isActive());
        QVERIFY(view.findChild<ReviewFindingCard *>()->isVisible());
    }

    void findingsAreGroupedByFile()
    {
        ReviewedFindings review;

        QCOMPARE(review.list.findChildren<ReviewFileHeader *>().size(), 2);
        QCOMPARE(review.list.findChildren<ReviewFindingCard *>().size(), 3);
    }

    void theErrorsFilterShowsOnlyTheErrors()
    {
        ReviewedFindings review;

        review.list.setFilter(ReviewFindingsList::Filter::Errors);

        const QList<ReviewFindingCard *> cards = review.list.findChildren<ReviewFindingCard *>();
        QCOMPARE(cards.size(), 1);
        QCOMPARE(cards.first()->finding().problem, QString("Off by one."));
    }

    void deleteDismissesTheCurrentFindingAndMakesTheNextCurrent()
    {
        ReviewedFindings review;
        review.list.setFocus();

        QTest::keyClick(&review.list, Qt::Key_Down);
        QTest::keyClick(&review.list, Qt::Key_Delete);

        QCOMPARE(review.session.findings().size(), 2);
        QVERIFY(std::ranges::none_of(review.session.findings(), [](const ReviewReport::Finding &finding) {
            return finding.problem == "Unused value.";
        }));
        QCOMPARE(review.list.selectedFindings().size(), 1);
        QCOMPARE(review.list.selectedFindings().first().problem, QString("Leaks the file."));
    }

    void resolveAndSendLeaveOutTheFindingsHandedOverToBeFixed()
    {
        ReviewedFindings review;
        const QSignalSpy resolved(&review.session, &ReviewSession::resolveRequested);
        const QSignalSpy sent(&review.session, &ReviewSession::sendRequested);
        const ReviewReport::Finding first = review.session.findings().first();
        review.session.resolve({first});

        review.session.resolve(review.session.findings());
        review.session.send({first});
        review.session.sendAll();

        QCOMPARE(resolved.size(), 2);
        const QString second = resolved.last().first().toString();
        QVERIFY(!second.contains("Off by one."));
        QVERIFY(second.contains("Unused value.") && second.contains("Leaks the file."));
        QCOMPARE(sent.size(), 0);
    }

    void afterResolveAllOnlyDismissIsLeft()
    {
        ReviewedFindings review;
        ReviewFindingsView view;
        view.setSession(&review.session);
        view.show();
        // The buttons of the cards, not those of the bar.
        const auto buttonsNamed = [&view](const QString &text) {
            qsizetype count = 0;
            for (const ReviewFindingCard *card : view.findChildren<ReviewFindingCard *>()) {
                const QList<QPushButton *> buttons = card->findChildren<QPushButton *>();
                count += std::ranges::count_if(buttons,
                                               [&text](const QPushButton *button) { return button->text() == text; });
            }
            return count;
        };
        const auto barButton = [&view](const QString &text) {
            for (QPushButton *button : view.findChildren<QPushButton *>(Qt::FindDirectChildrenOnly)) {
                if (button->text() == text)
                    return button;
            }
            return static_cast<QPushButton *>(nullptr);
        };

        barButton("Resolve All")->click();

        QTRY_COMPARE(buttonsNamed("Dismiss"), qsizetype(3));
        QVERIFY(!barButton("Resolve All")->isEnabled());
        QVERIFY(!barButton("Send All to Claude")->isEnabled());
        QCOMPARE(buttonsNamed("Send to Claude"), qsizetype(0));
        QCOMPARE(buttonsNamed("Resolve"), qsizetype(0));
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
