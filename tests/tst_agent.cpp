// SPDX-License-Identifier: GPL-3.0-only

#include "agent/AgentActivity.h"
#include "agent/AgentIndicator.h"
#include "agent/AgentProviders.h"
#include "agent/DiagramRequestPopup.h"
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
#include "agent/ReviewText.h"
#include "agent/ReviewView.h"
#include "agent/vendor/claude/ClaudeCodeProvider.h"
#include "diagnostics/ProblemStore.h"
#include "support/FakeReviewer.h"
#include "ui/Snackbar.h"
#include "ui/StepTimeline.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
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

QPushButton *buttonWithText(const QWidget &widget, const QString &text)
{
    for (QPushButton *button : widget.findChildren<QPushButton *>()) {
        if (button->text() == text)
            return button;
    }
    return nullptr;
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

// A hook's connection to `server`, as kodamiko-hook makes it.
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
        QPushButton *start = buttonWithText(page, "Review the Staged Changes");
        if (!start)
            QFAIL("The invitation has no start button");
        QVERIFY(start->isVisible());
        QVERIFY(!page.findChild<ReviewView *>());
        const QSignalSpy requested(&page, &ReviewPage::reviewRequested);
        start->click();
        QCOMPARE(requested.size(), 1);

        page.setSession(&session);
        const auto *findings = page.findChild<ReviewFindingsView *>();
        QVERIFY(findings);
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

    void aCleanReviewKeepsItsSummaryWithItsHeading()
    {
        const QTemporaryDir folder;
        const FakeReviewer agent(QStringLiteral(R"({"summary": "Sound.", "findings": []})"));
        ReviewView view({.workDir = folder.path(), .subject = "the code", .agent = &agent});
        view.resize(800, 900);
        view.show();

        view.session()->start();
        QTRY_COMPARE(view.session()->state(), ReviewSession::State::Finished);

        const QLabel *summary = nullptr;
        for (const QLabel *label : view.findChildren<QLabel *>()) {
            if (label->isVisible() && label->text().contains("Sound."))
                summary = label;
        }
        QVERIFY(summary);
        const auto *title = view.findChild<QLabel *>("reviewCleanTitle");
        QVERIFY(title && title->isVisible());
        const int gap = summary->mapTo(&view, QPoint()).y() - title->mapTo(&view, title->rect().bottomLeft()).y();
        QVERIFY2(gap >= 0 && gap < 60, "the summary is separated from its heading");
    }

    void returnInTheDiagramsFileNameDrawsOnce()
    {
        QWidget window;
        window.resize(900, 700);
        DiagramRequestPopup popup(Theme::kodamiko(), &window);
        window.show();
        int drawings = 0;
        popup.popup({.location = "docs", .kinds = {DiagramKinds::find("flowchart")}, .initialName = "flows/login"},
                    [&drawings](const QString &, const QString &, const QString &) {
                        ++drawings;
                        return QString();
                    });
        auto *name = popup.findChild<QLineEdit *>();
        QVERIFY(name);

        QTest::keyClick(name, Qt::Key_Return);

        QCOMPARE(drawings, 1);
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

        barButton("Fix visible")->click();

        QTRY_COMPARE(buttonsNamed("Dismiss"), qsizetype(3));
        QVERIFY(!barButton("Fix visible")->isEnabled());
        QVERIFY(!barButton("Prepare prompt")->isEnabled());
        QCOMPARE(buttonsNamed("Prepare prompt"), qsizetype(0));
        QCOMPARE(buttonsNamed("Ask Claude to fix"), qsizetype(0));
    }

    void anInterruptedReviewFreezesItsClockAndCanStartAgain()
    {
        const QTemporaryDir folder;
        const FakeReviewer agent(FakeReviewer::oneFindingReport);
        ReviewView view({.workDir = folder.path(), .subject = "the code", .agent = &agent});
        view.show();
        view.session()->start();

        buttonWithText(view, "Stop review")->click();

        QCOMPARE(view.session()->state(), ReviewSession::State::Interrupted);
        const auto elapsed = view.session()->elapsed();
        QTest::qWait(30);
        QCOMPARE(view.session()->elapsed(), elapsed);
        QVERIFY(shownTexts(view).contains("The review did not finish."));
        QVERIFY(view.session()->findings().isEmpty());
        buttonWithText(*view.findChild<ReviewProgressView *>(), "Review Again")->click();
        QTRY_COMPARE(view.session()->state(), ReviewSession::State::Finished);
    }

    void undoDismissRestoresTheOriginalOrderAndHandoffStatus()
    {
        ReviewedFindings review;
        const auto original = review.session.findings();
        review.session.resolve({original.first()});
        review.session.dismiss({original.first(), original.last()});
        QCOMPARE(review.session.findings().size(), 1);
        QCOMPARE(review.session.reportedFindingCount(), 3);

        review.session.undoDismiss();

        QCOMPARE(review.session.findings(), original);
        QVERIFY(review.session.isResolving(original.first()));
        QVERIFY(!review.session.canUndoDismiss());
    }

    void undoDismissRepublishesTheFindingInTheEditor()
    {
        const QTemporaryDir folder;
        const FakeReviewer agent(FakeReviewer::oneFindingReport);
        ProblemStore problems(nullptr);
        ReviewSession session(
            {.workDir = folder.path(), .subject = "the code", .agent = &agent, .problems = &problems});
        session.start();
        QTRY_COMPARE(session.state(), ReviewSession::State::Finished);
        const QString path = session.findings().first().path;
        session.dismiss(session.findings());
        QVERIFY(problems.reviewProblems(path).isEmpty());

        session.undoDismiss();

        QCOMPARE(problems.reviewProblems(path).size(), 1);
    }

    void aCleanReviewKeepsTheFailedCheckVisible()
    {
        const QTemporaryDir folder;
        const FakeReviewer agent(QStringLiteral(R"({"summary":"No concrete issue.","findings":[]})"));
        ReviewView view({.workDir = folder.path(), .subject = "the code", .checkCommand = "exit 3", .agent = &agent});
        view.show();
        view.session()->start();
        QTRY_COMPARE(view.session()->state(), ReviewSession::State::CheckFailed);

        view.session()->reviewAnyway();

        QTRY_COMPARE(view.session()->state(), ReviewSession::State::Finished);
        QVERIFY(shownTexts(view).contains("The project check failed. It still needs attention before committing."));
    }

    void aNewRunForgetsThePreviousDismissals()
    {
        ReviewedFindings review;
        review.session.dismiss(review.session.findings());
        QVERIFY(review.session.canUndoDismiss());

        review.session.start();

        QVERIFY(!review.session.canUndoDismiss());
        QTRY_COMPARE(review.session.state(), ReviewSession::State::Finished);
        review.session.undoDismiss();
        QCOMPARE(review.session.findings().size(), 3);
    }

    void aFilterClearsSelectionAndLimitsTheBulkHandoff()
    {
        ReviewedFindings review;
        ReviewFindingsView view;
        view.setSession(&review.session);
        view.show();
        auto *list = view.findChild<ReviewFindingsList *>();
        QTest::keyClick(list, Qt::Key_Up);
        QCOMPARE(list->selectedFindings().size(), 1);
        const QSignalSpy resolved(&review.session, &ReviewSession::resolveRequested);

        for (QToolButton *button : view.findChildren<QToolButton *>()) {
            if (button->text() == "Warnings 2")
                button->click();
        }
        QVERIFY(list->selectedFindings().isEmpty());
        buttonWithText(view, "Fix visible")->click();

        QCOMPARE(resolved.size(), 1);
        const QString prompt = resolved.first().first().toString();
        QVERIFY(!prompt.contains("Off by one."));
        QVERIFY(prompt.contains("Unused value.") && prompt.contains("Leaks the file."));
    }

    void theLastDismissedFindingCanBeRestoredFromTheReview()
    {
        const QTemporaryDir folder;
        const FakeReviewer agent(FakeReviewer::oneFindingReport);
        ReviewView view({.workDir = folder.path(), .subject = "the code", .agent = &agent});
        view.show();
        view.session()->start();
        QTRY_COMPARE(view.session()->state(), ReviewSession::State::Finished);

        view.session()->dismiss(view.session()->findings());

        QVERIFY(view.findChild<ReviewFindingsView *>()->isVisible());
        QVERIFY(ReviewText::summary(*view.session()).contains("dismissed"));
        auto *undo = buttonWithText(view, "Undo dismissal");
        QVERIFY(undo && undo->isVisible());
        undo->click();
        QCOMPARE(view.session()->findings().size(), 1);
        QVERIFY(view.findChild<ReviewFindingCard *>()->isVisible());
    }

    void retryingTheReviewPageRequestsFreshStagedChanges()
    {
        ReviewedFindings review;
        ReviewPage page;
        page.setSession(&review.session);
        page.show();
        const QSignalSpy requested(&page, &ReviewPage::reviewRequested);
        const QSignalSpy restarted(&review.session, &ReviewSession::stateChanged);

        const auto *view = page.findChild<ReviewView *>();
        for (QPushButton *button : view->findChildren<QPushButton *>()) {
            if (button->text() == "Review Again" && button->isVisible())
                button->click();
        }

        QCOMPARE(requested.size(), 1);
        QCOMPARE(restarted.size(), 0);
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
