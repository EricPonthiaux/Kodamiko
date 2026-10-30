// SPDX-License-Identifier: GPL-3.0-only

// MainWindow: the reviews in a separate agent process, in a tab of the
// editor, over the diff, or in the Review mode.

#include "MainWindow.h"
#include "agent/ReviewPage.h"
#include "agent/ReviewRequest.h"
#include "agent/ReviewView.h"
#include "changes/ChangesView.h"
#include "editor/EditorTabs.h"
#include "git/FileChange.h"
#include "git/GitCommands.h"
#include "git/RepositoryStatus.h"
#include "navigation/CodeNavigator.h"

#include <QActionGroup>
#include <QFileInfo>
#include <QMenu>
#include <QSettings>
#include <QTabBar>

#include <algorithm>
#include <memory>

namespace {

// QSettings key of the model of the review runs: an AgentModel::id, empty
// for the agent's default.
const QString reviewerModelKey = QStringLiteral("reviewerModel");

// The staged changes of `status` as one patch for the reviewer.
QByteArray stagedPatch(const RepositoryStatus &status)
{
    QByteArray patch;
    for (const FileDiff &file : status.staged) {
        if (file.isTooLarge)
            patch += "# Too large to show: " + file.change.path.toUtf8() + "\n";
        else
            patch += file.patch;
    }
    return patch;
}

// QSettings key of the review step: true while the Review mode is shown
// between Edit and Diff.
const QString reviewStepKey = QStringLiteral("reviewStep");

} // namespace

void MainWindow::reviewCode()
{
    const std::optional<CodeRequest::Target> target = currentCodeTarget();
    if (!target)
        return;
    if (target->unsaved) {
        showStatus(tr("Save the file first: the review reads it from the disk"));
        return;
    }
    openReview(CodeRequest::describe(*target), QFileInfo(target->path).fileName());
}

void MainWindow::reviewPaths(const QStringList &paths)
{
    const bool isUnsaved =
        std::ranges::any_of(paths, [this](const QString &path) { return editorTabs_->hasUnsavedChangesUnder(path); });
    if (isUnsaved) {
        showStatus(tr("Save the files first: the review reads them from the disk"));
        return;
    }
    const QString title =
        paths.size() == 1 ? QFileInfo(paths.first()).fileName() : tr("%n Entries", "", int(paths.size()));
    openReview(CodeRequest::describe(paths, folder_), title);
}

void MainWindow::reviewStagedChanges()
{
    if (editorTabs_->hasUnsavedChangesUnder(folder_)) {
        showStatus(tr("Save the files first: the review reads them from the disk"));
        return;
    }
    // Filled by the job, off the UI thread, then read on it: shared by the two.
    const auto patch = std::make_shared<QByteArray>();
    gitCommands_->runQuietly(
        [folder = folder_, patch] {
            const std::optional<RepositoryStatus> status = RepositoryStatus::read(folder);
            if (!status)
                throw GitError(QObject::tr("The folder is not in a git repository"));
            *patch = stagedPatch(*status);
            return QString();
        },
        [this, patch, folder = folder_](const QString &, const QString &error) {
            if (folder != folder_)
                return; // another project opened meanwhile: the patch is the previous one's
            if (!error.isEmpty()) {
                showStatus(tr("Could not read the changes to review: %1").arg(error));
                return;
            }
            if (patch->isEmpty()) {
                showStatus(tr("No staged changes to review: stage the changes to commit first"));
                return;
            }
            showStagedReview(new ReviewSession(reviewRequest(tr("the staged changes"), *patch), this));
        });
}

void MainWindow::showStagedReview(ReviewSession *session)
{
    // The views first let go of the review they follow.
    const bool isStep = isReviewStepOn();
    changesView_->setReview(session,
                            isStep ? ChangesView::ReviewDisplay::NotesOnly : ChangesView::ReviewDisplay::OverTheDiff);
    reviewPage_->setSession(isStep ? session : nullptr);
    if (stagedReview_)
        stagedReview_->deleteLater();
    stagedReview_ = session;
    setMode(isStep ? Mode::Review : Mode::Diff);
    sendReviewsToPrompt(session);
    session->start();
}

void MainWindow::closeStagedReview()
{
    changesView_->setReview(nullptr, ChangesView::ReviewDisplay::OverTheDiff);
    reviewPage_->setSession(nullptr);
    if (stagedReview_)
        stagedReview_->deleteLater();
    stagedReview_ = nullptr;
}

bool MainWindow::isReviewStepOn() const
{
    return QSettings().value(reviewStepKey, false).toBool();
}

void MainWindow::setReviewStep(bool on)
{
    QSettings().setValue(reviewStepKey, on);
    modeTabs_->setTabVisible(int(Mode::Review), on);
    modeActions_->actions().at(int(Mode::Review))->setVisible(on);
    // The review shown moves to the place the step gives it.
    changesView_->setReview(stagedReview_,
                            on ? ChangesView::ReviewDisplay::NotesOnly : ChangesView::ReviewDisplay::OverTheDiff);
    reviewPage_->setSession(on ? stagedReview_.data() : nullptr);
    if (!on && mode_ == Mode::Review)
        setMode(Mode::Diff);
}

QWidget *MainWindow::createReviewPage()
{
    reviewPage_ = new ReviewPage(this);
    reviewPage_->setAgentName(agentProvider_->name());
    connect(reviewPage_, &ReviewPage::reviewRequested, this, &MainWindow::reviewStagedChanges);
    connect(reviewPage_, &ReviewPage::closeRequested, this, &MainWindow::closeStagedReview);
    connect(reviewPage_, &ReviewPage::inspectRequested, this, [this] { setMode(Mode::Diff); });
    connect(reviewPage_, &ReviewPage::findingRequested, this, [this](const ReviewReport::Finding &finding) {
        setMode(Mode::Diff);
        changesView_->goToFinding(finding);
    });
    return reviewPage_;
}

ReviewSession::Request MainWindow::reviewRequest(const QString &subject, const QByteArray &patch) const
{
    return {.workDir = folder_,
            .subject = subject,
            .patch = patch,
            .checkCommand = project_.agentCheckCommand.trimmed(),
            .model = QSettings().value(reviewerModelKey).toString(),
            .agent = agentProvider_,
            .problems = problemStore_};
}

void MainWindow::sendReviewsToPrompt(ReviewSession *session)
{
    const QString &subject = session->subject();
    connect(session, &ReviewSession::sendRequested, this, [this, subject](const QString &review) {
        typeInClaudePrompt(ReviewRequest::followUp(agentProvider_->name(), subject, review), PromptEntry::Typed);
    });
    connect(session, &ReviewSession::resolveRequested, this, [this, subject](const QString &review) {
        typeInClaudePrompt(ReviewRequest::fixRequest(agentProvider_->name(), subject, review), PromptEntry::Submitted);
    });
}

void MainWindow::openReview(const QString &subject, const QString &title)
{
    setMode(Mode::Edit);
    auto *view = new ReviewView(reviewRequest(subject));
    editorTabs_->showPage(view, tr("Review: %1").arg(title), agentProvider_->iconName());
    connect(view, &ReviewView::closeRequested, this, [this, view] { editorTabs_->closePage(view); });
    connect(view, &ReviewView::openRequested, this, [this](const QString &path, int line) {
        setMode(Mode::Edit);
        navigator_->goToLine(path, line - 1, 0);
    });
    sendReviewsToPrompt(view->session());
    view->session()->start();
}

void MainWindow::fillReviewerModelMenu(QMenu *menu)
{
    menu->clear();
    // The previous opening's group, without its actions now.
    qDeleteAll(menu->findChildren<QActionGroup *>(Qt::FindDirectChildrenOnly));
    auto *group = new QActionGroup(menu);
    const QString chosen = QSettings().value(reviewerModelKey).toString();
    const auto add = [this, menu, group, &chosen](const QString &title, const QString &id) {
        QAction *action = menu->addAction(title);
        action->setCheckable(true);
        action->setChecked(id == chosen);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [id] { QSettings().setValue(reviewerModelKey, id); });
    };
    add(tr("&Default (%1's Settings)").arg(agentProvider_->name()), QString());
    for (const AgentModel &model : agentProvider_->models())
        add(model.title, model.id);
}
