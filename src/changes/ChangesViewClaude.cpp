// SPDX-License-Identifier: GPL-3.0-only

// ChangesView: the review of the changes: the files Claude edited, the
// blocks staged so far, Only Claude's, and a block explained by the agent.

#include "ChangesView.h"
#include "DiffView.h"
#include "HunkActions.h"
#include "ui/AppFonts.h"

#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QToolButton>

namespace {

constexpr int progressWidthPx = 140;
constexpr int progressHeightPx = 5;

// Blocks of changes: all of them, and those of the files Claude edited.
struct BlockCount {
    int all = 0;
    int byClaude = 0;
};

} // namespace

void ChangesView::markEditedByClaude(const QStringList &absolutePaths)
{
    for (const QString &path : absolutePaths)
        claudeEdited_.insert(QDir::cleanPath(path));
    if (status_)
        showDiff(); // the marks, and Only Claude's
}

bool ChangesView::isEditedByClaude(const FileChange &change) const
{
    return claudeEdited_.contains(QDir::cleanPath(QDir(folder_).filePath(change.path)));
}

void ChangesView::forgetCleanClaudeEdits()
{
    QSet<QString> stillChanged;
    for (const Section section : {Section::Staged, Section::Unstaged}) {
        for (const FileDiff &diff : files(section)) {
            if (isEditedByClaude(diff.change))
                stillChanged.insert(QDir::cleanPath(QDir(folder_).filePath(diff.change.path)));
        }
    }
    claudeEdited_ = stillChanged;
}

QWidget *ChangesView::createReviewFooter(QWidget *parent)
{
    auto *footer = new QFrame(parent);
    footer->setObjectName(QStringLiteral("reviewFooter"));
    reviewProgress_ = new QProgressBar(footer);
    reviewProgress_->setTextVisible(false);
    reviewProgress_->setFixedSize(progressWidthPx, progressHeightPx);
    reviewCount_ = new QLabel(footer);
    reviewCount_->setObjectName(QStringLiteral("historyMuted"));
    onlyClaude_ = new QToolButton(footer);
    onlyClaude_->setText(tr("Only Claude's"));
    onlyClaude_->setToolTip(tr("Show only the files Claude edited since they were last committed"));
    onlyClaude_->setCheckable(true);
    onlyClaude_->setAutoRaise(true);
    onlyClaude_->setFocusPolicy(Qt::NoFocus);
    AppFonts::setInterfaceFont(onlyClaude_, 0.9, QFont::Normal);
    connect(onlyClaude_, &QToolButton::toggled, this, &ChangesView::showDiff);
    auto *layout = new QHBoxLayout(footer);
    layout->setContentsMargins(12, 4, 8, 4);
    layout->setSpacing(10);
    layout->addWidget(reviewProgress_);
    layout->addWidget(reviewCount_, 1);
    layout->addWidget(onlyClaude_);
    return footer;
}

void ChangesView::updateReviewFooter()
{
    const auto countBlocks = [this](Section section) {
        BlockCount count;
        for (const FileDiff &diff : files(section)) {
            const int blocks = int(FilePatch::parse(diff.patch).hunks.size());
            count.all += blocks;
            count.byClaude += isEditedByClaude(diff.change) ? blocks : 0;
        }
        return count;
    };
    const BlockCount staged = countBlocks(Section::Staged);
    const BlockCount unstaged = countBlocks(Section::Unstaged);
    const int all = staged.all + unstaged.all;
    const int byClaude = staged.byClaude + unstaged.byClaude;
    reviewProgress_->setRange(0, std::max(1, all));
    reviewProgress_->setValue(staged.all);
    QString text =
        all == 1 ? tr("%1 of 1 block staged").arg(staged.all) : tr("%1 of %2 blocks staged").arg(staged.all).arg(all);
    if (byClaude > 0)
        text += tr(" · %1 by Claude").arg(byClaude);
    reviewCount_->setText(all > 0 ? text : QString());
    reviewProgress_->setVisible(all > 0);
    // Without Claude's edits, nothing to keep; checked, it stays: what it
    // hides may be all there was.
    onlyClaude_->setVisible(byClaude > 0 || onlyClaude_->isChecked());
}

void ChangesView::askAboutBlock(int line)
{
    const DiffLocation location = diff_->locate(line);
    if (location.file < 0 || location.file >= shownPatches_.size() || location.hunk < 0)
        return;
    const DiffHunk &hunk = shownPatches_.at(location.file).hunks.at(location.hunk);
    const QString path = changeOf(shownFiles_.at(location.file)).path;
    QStringList lines;
    for (const QByteArray &hunkLine : hunk.lines)
        lines.append(QString::fromUtf8(hunkLine));
    const int lastLine = hunk.newStart + std::max(0, hunk.newCount - 1);
    emit explanationRequested(
        tr("Explain this change to %1 (lines %2–%3): what it does, why it is there, and whether it is right. "
           "Do not edit any file.\n\n```diff\n@@ -%4,%5 +%2,%6 @@\n%7\n```")
            .arg(path)
            .arg(hunk.newStart)
            .arg(lastLine)
            .arg(hunk.oldStart)
            .arg(hunk.oldCount)
            .arg(hunk.newCount)
            .arg(lines.join(QLatin1Char('\n'))));
}
