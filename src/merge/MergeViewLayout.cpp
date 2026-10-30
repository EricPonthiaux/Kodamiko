// SPDX-License-Identifier: GPL-3.0-only

#include "ConflictNavigator.h"
#include "MergeReadyPanel.h"
#include "MergeTextPanel.h"
#include "MergeView.h"
#include "git/GitCommands.h"
#include "ui/AppFonts.h"
#include "ui/ViewHeader.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int barMarginPx = 16;
constexpr int barSpacingPx = 8;

} // namespace

// The operation and how far it is, as the Changes and History views have
// their title and summary.
QWidget *MergeView::createTitleBar()
{
    title_ = new QLabel(this);
    title_->setTextFormat(Qt::PlainText);
    AppFonts::setViewTitleFont(title_);
    summary_ = new QLabel(this);
    summary_->setEnabled(false); // dimmed
    auto *titleBar = new QFrame(this);
    titleBar->setObjectName(QStringLiteral("viewTitleBar"));
    titleBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *layout = new QHBoxLayout(titleBar);
    layout->setContentsMargins(16, 10, 12, 10);
    layout->setSpacing(10);
    layout->addWidget(title_);
    layout->addWidget(summary_);
    layout->addStretch();
    return titleBar;
}

// The conflicted files under a header, as the file tree is.
QWidget *MergeView::createSidebar()
{
    auto *sidebar = new QWidget(this);
    sidebar->setObjectName(QStringLiteral("mergeSidebar"));
    filesHeader_ = new ViewHeader(tr("Files to Resolve"), sidebar);
    auto *legend = new QLabel(tr("Changes without conflicts are already combined."), sidebar);
    legend->setObjectName(QStringLiteral("mergeLegend"));
    legend->setWordWrap(true);
    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(filesHeader_);
    layout->addWidget(list_, 1);
    layout->addWidget(legend);
    return sidebar;
}

// The file shown and its actions, level with the files' header.
QWidget *MergeView::createBar()
{
    fileTitle_ = new QLabel(this);
    fileTitle_->setTextFormat(Qt::PlainText);
    fileHeader_ = new ViewHeader(fileTitle_, this);
    AppFonts::setInterfaceFont(fileTitle_, 1.0, QFont::DemiBold);
    // Where the view stands among the conflicts, before the actions.
    navigator_ = new ConflictNavigator(theme_, fileHeader_);
    fileHeader_->addWidget(navigator_);
    connect(navigator_, &ConflictNavigator::previousRequested, this, [this] {
        if (panel_)
            panel_->goToConflict(MergeTextPanel::Direction::Previous);
    });
    connect(navigator_, &ConflictNavigator::nextRequested, this, [this] {
        if (panel_)
            panel_->goToConflict(MergeTextPanel::Direction::Next);
    });
    connect(navigator_, &ConflictNavigator::conflictRequested, this, [this](int index) {
        if (panel_)
            panel_->goToConflict(index);
    });
    connect(navigator_, &ConflictNavigator::nextOpenRequested, this, [this] {
        if (panel_)
            panel_->goToNextOpenConflict();
    });
    resolveSimple_ = fileHeader_->addTextButton(
        QStringLiteral("check"), tr("Resolve Simple Conflicts"),
        tr("Resolve the conflicts where the sides differ only by whitespace or changed different words"));
    connect(resolveSimple_, &QToolButton::clicked, this, &MergeView::resolveSimpleConflicts);
    askAgent_ = fileHeader_->addTextButton(QStringLiteral("claude"), QString(), QString());
    connect(askAgent_, &QToolButton::clicked, this, &MergeView::askOrStopAgent);
    return fileHeader_;
}

QWidget *MergeView::createFooter()
{
    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("mergeFooter"));
    hint_ = new QLabel(footer);
    hint_->setObjectName(QStringLiteral("pageDescription"));
    save_ = new QPushButton(tr("Save Resolution"), footer);
    save_->setToolTip(tr("Write the result to the file and mark it resolved in git"));
    abort_ = new QPushButton(footer);
    continue_ = new QPushButton(footer);
    continue_->setDefault(true);
    reviewFiles_ = new QPushButton(tr("Review Files"), footer);
    connect(reviewFiles_, &QPushButton::clicked, this, &MergeView::toggleReview);
    close_ = new QPushButton(tr("Close"), footer);
    connect(save_, &QPushButton::clicked, this, &MergeView::saveCurrentFile);
    connect(abort_, &QPushButton::clicked, this, &MergeView::abortRequested);
    connect(continue_, &QPushButton::clicked, this, &MergeView::continueRequested);
    connect(close_, &QPushButton::clicked, this, &MergeView::closeRequested);
    auto *layout = new QHBoxLayout(footer);
    layout->setContentsMargins(barMarginPx, barMarginPx, barMarginPx, barMarginPx);
    layout->setSpacing(barSpacingPx);
    hint_->setWordWrap(true);
    layout->addWidget(abort_);
    layout->addWidget(hint_, 1);
    layout->addWidget(save_);
    layout->addWidget(reviewFiles_);
    layout->addWidget(continue_);
    layout->addWidget(close_);
    return footer;
}

void MergeView::updateFooter()
{
    const QString &operation = conflicts_.repository.operation;
    const bool hasOperation = Integration::canContinue(operation);
    const bool isDone = hasRead_ && conflicts_.error.isEmpty() && conflicts_.files.isEmpty();
    const ConflictVersions::File *file = currentFile();
    const MergeDocument *document = file ? openedDocument(file->path) : nullptr;
    const bool busy = finalizing_ || commands_->isBusy();
    const bool canSave = document && document->isResolved() && !busy;
    save_->setVisible(!isDone && document);
    save_->setEnabled(canSave);
    save_->setDefault(!isDone);
    abort_->setText(tr("Abort %1").arg(operation));
    abort_->setVisible(hasOperation);
    abort_->setEnabled(!busy);
    updateFinishButtons(isDone, hasOperation, busy);
    hint_->setText(footerHint(document, isDone));
}

void MergeView::updateFinishButtons(bool isDone, bool hasOperation, bool busy)
{
    const QString &operation = conflicts_.repository.operation;
    QString text = operation == tr("merge") ? tr("Finish Merge") : tr("Continue %1").arg(operation);
    if (finalizing_)
        text = tr("Finishing…");
    continue_->setText(text);
    continue_->setVisible(hasOperation && isDone);
    continue_->setDefault(isDone);
    const bool hasMessage = operation != tr("merge") || !ready_->message().trimmed().isEmpty();
    continue_->setEnabled(isDone && !busy && hasMessage);
    reviewFiles_->setVisible(hasOperation && isDone && !conflicts_.staged.isEmpty());
    reviewFiles_->setEnabled(!busy);
    close_->setVisible(isDone && !hasOperation);
}

QString MergeView::footerHint(const MergeDocument *document, bool isDone) const
{
    const QString &operation = conflicts_.repository.operation;
    const bool hasOperation = Integration::canContinue(operation);
    if (finalizing_)
        return tr("Git is recording the result…");
    if (isDone)
        return hasOperation ? tr("Every file is resolved: the %1 can go on.").arg(operation) : QString();
    if (!document)
        return tr("A choice resolves the file at once; Kodamiko takes a snapshot first.");
    if (!document->isResolved())
        return tr("Take a side, both, or type the result for each conflict, then save.");
    return tr("Saving writes the result and marks the file resolved.");
}
