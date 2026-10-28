// SPDX-License-Identifier: GPL-3.0-only

// ChangesView: the review of the staged changes, its bar over the diff and
// its findings under the diff's lines.

#include "ChangesView.h"
#include "DiffView.h"
#include "agent/ReviewBar.h"
#include "agent/ReviewFindingsPanel.h"

#include <QDir>
#include <QEvent>
#include <QHBoxLayout>
#include <QMenu>

#include <algorithm>

namespace {

// What a note says of `finding`: its severity and problem, then the
// scenario where the code fails.
QString noteText(const ReviewReport::Finding &finding)
{
    const QString severity =
        finding.severity == Diagnostic::Severity::Error ? ChangesView::tr("Error") : ChangesView::tr("Warning");
    QString text = QStringLiteral("%1 · %2").arg(severity, finding.problem);
    if (!finding.scenario.isEmpty())
        text += QLatin1Char('\n') + finding.scenario;
    return text;
}

} // namespace

QWidget *ChangesView::createReviewArea()
{
    reviewBar_ = new ReviewBar(this);
    connect(reviewBar_, &ReviewBar::closeRequested, this, &ChangesView::reviewCloseRequested);
    connect(reviewBar_, &ReviewBar::findingsRequested, this, &ChangesView::showFindingsPanel);
    // A new review, of the staged changes read again (setReview()).
    connect(reviewBar_, &ReviewBar::reviewAgainRequested, this, &ChangesView::reviewRequested);
    // Its margins hide with it, as the operation's bar's.
    reviewArea_ = new QWidget(this);
    auto *row = new QHBoxLayout(reviewArea_);
    row->setContentsMargins(8, 8, 8, 8);
    row->addWidget(reviewBar_);
    reviewArea_->hide();
    return reviewArea_;
}

void ChangesView::createFindingsPanel(QWidget *diffPane)
{
    findingsPanel_ = new ReviewFindingsPanel(diffPane);
    findingsPanel_->hide();
    connect(findingsPanel_, &ReviewFindingsPanel::closeRequested, this, &ChangesView::hideFindingsPanel);
    connect(findingsPanel_, &ReviewFindingsPanel::findingRequested, this, [this](const ReviewReport::Finding &finding) {
        hideFindingsPanel();
        goToFinding(finding);
    });
    diff_->installEventFilter(this);
}

bool ChangesView::eventFilter(QObject *watched, QEvent *event)
{
    const bool isDiffMoved = watched == diff_ && (event->type() == QEvent::Resize || event->type() == QEvent::Move);
    if (isDiffMoved)
        findingsPanel_->setGeometry(diff_->geometry());
    return QWidget::eventFilter(watched, event);
}

void ChangesView::showFindingsPanel()
{
    findingsPanel_->setGeometry(diff_->geometry());
    findingsPanel_->show();
    findingsPanel_->raise();
    findingsPanel_->setFocus();
}

void ChangesView::hideFindingsPanel()
{
    findingsPanel_->hide();
    diff_->setFocus();
}

void ChangesView::onReviewStateChanged(ReviewSession::State state)
{
    const bool hasFindings = state == ReviewSession::State::Finished && review_ && !review_->findings().isEmpty();
    if (hasFindings && reviewDisplay_ == ReviewDisplay::OverTheDiff)
        showFindingsPanel();
    else
        findingsPanel_->hide();
    showFindings();
}

void ChangesView::setReview(ReviewSession *session, ReviewDisplay display)
{
    if (review_)
        review_->disconnect(this);
    review_ = session;
    reviewDisplay_ = display;
    const bool isOverTheDiff = session && display == ReviewDisplay::OverTheDiff;
    reviewBar_->setSession(isOverTheDiff ? session : nullptr);
    findingsPanel_->setSession(isOverTheDiff ? session : nullptr);
    findingsPanel_->hide();
    reviewArea_->setVisible(isOverTheDiff);
    if (session) {
        connect(session, &ReviewSession::stateChanged, this, &ChangesView::onReviewStateChanged);
        connect(session, &ReviewSession::findingsChanged, this, &ChangesView::showFindings);
    }
    showFindings();
}

void ChangesView::showFindings()
{
    noteFindings_.clear();
    QList<DiffNote> notes;
    if (review_) {
        for (const ReviewReport::Finding &finding : review_->findings()) {
            const int file = shownPartOf(finding);
            if (file < 0)
                continue;
            notes.append(
                {.file = file, .newLine = finding.line, .severity = finding.severity, .text = noteText(finding)});
            noteFindings_.append(finding);
        }
    }
    diff_->setNotes(notes);
}

int ChangesView::shownPartOf(const ReviewReport::Finding &finding) const
{
    if (!status_)
        return -1;
    const QString path = QDir(folder_).relativeFilePath(finding.path);
    int found = -1;
    for (int i = 0; i < shownFiles_.size(); ++i) {
        if (changeOf(shownFiles_[i]).path != path)
            continue;
        found = i;
        if (shownFiles_[i].section == Section::Unstaged)
            break;
    }
    return found;
}

void ChangesView::goToFinding(const ReviewReport::Finding &finding)
{
    if (!noteFindings_.contains(finding) && status_) {
        const QString path = QDir(folder_).relativeFilePath(finding.path);
        const auto isOfFinding = [&path](const FileDiff &diff) { return diff.change.path == path; };
        const bool isChanged =
            std::ranges::any_of(status_->staged, isOfFinding) || std::ranges::any_of(status_->unstaged, isOfFinding);
        if (!isChanged) {
            emit openFileRequested(path, finding.line - 1);
            return;
        }
        selectNow(path); // shows its diff, with its notes
    }
    diff_->showNote(int(noteFindings_.indexOf(finding)));
    diff_->setFocus();
}

void ChangesView::addFindingActions(QMenu *menu, QAction *before, int line)
{
    const QList<int> notes = diff_->notesAt(line);
    if (notes.isEmpty() || !review_)
        return;
    QList<ReviewReport::Finding> findings;
    for (const int note : notes)
        findings.append(noteFindings_.value(note));
    const QString agent = review_->agentName();
    const bool isSingle = findings.size() == 1;
    auto *send = new QAction(
        isSingle ? tr("Send This Finding to %1").arg(agent) : tr("Send These Findings to %1").arg(agent), menu);
    send->setEnabled(!review_->notHandedOver(findings).isEmpty());
    connect(send, &QAction::triggered, review_, [session = review_, findings] { session->send(findings); });
    auto *dismiss = new QAction(isSingle ? tr("Dismiss This Finding") : tr("Dismiss These Findings"), menu);
    connect(dismiss, &QAction::triggered, review_, [session = review_, findings] { session->dismiss(findings); });
    menu->insertAction(before, send);
    menu->insertAction(before, dismiss);
    menu->insertSeparator(before);
}
