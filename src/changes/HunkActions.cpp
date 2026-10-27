// SPDX-License-Identifier: GPL-3.0-only

#include "HunkActions.h"

#include "DiffView.h"
#include "ui/AppFonts.h"
#include "ui/Theme.h"
#include "ui/UiIcons.h"
#include "ui/UiMetrics.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

#include <algorithm>

namespace {

constexpr int rightMargin = 8;
constexpr int iconSize = 14;

QToolButton *actionButton(QFrame *bar, const QString &text, const QString &toolTip)
{
    auto *button = new QToolButton(bar);
    button->setText(text);
    button->setToolTip(toolTip);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    UiMetrics::setScaledIconSize(button, iconSize);
    AppFonts::setInterfaceFont(button, 0.9, QFont::Normal);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    bar->layout()->addWidget(button);
    return button;
}

} // namespace

HunkActions::HunkActions(DiffView *diff)
    : QObject(diff)
    , diff_(diff)
{
    diff_->viewport()->installEventFilter(this); // resized
    connect(diff_, &DiffView::contentChanged, this, &HunkActions::placeBars);
    // However it scrolls (wheel, keys, scroll bar), the bars follow their blocks.
    connect(diff_, &QsciScintillaBase::SCN_UPDATEUI, this, &HunkActions::placeBars);
}

void HunkActions::setStagedFiles(const QSet<int> &files)
{
    stagedFiles_ = files;
    placeBars();
}

void HunkActions::setClaudeFiles(const QSet<int> &files)
{
    claudeFiles_ = files;
    placeBars();
}

void HunkActions::setEnabled(bool enabled)
{
    isEnabled_ = enabled;
    for (const Bar &bar : std::as_const(bars_)) {
        bar.apply->setEnabled(enabled);
        bar.discard->setEnabled(enabled);
    }
}

void HunkActions::applyTheme(const Theme &theme)
{
    stageIcon_ = UiIcons::icon(QStringLiteral("plus"), theme.mutedText);
    unstageIcon_ = UiIcons::icon(QStringLiteral("minus"), theme.mutedText);
    discardIcon_ = UiIcons::icon(QStringLiteral("discard"), theme.mutedText);
    askIcon_ = UiIcons::icon(QStringLiteral("claude"), theme.claude);
    claudeColor_ = theme.claude;
    for (Bar &bar : bars_)
        styleBar(bar);
    placeBars();
}

QList<int> HunkActions::shownBlocks() const
{
    QList<int> blocks;
    for (const Bar &bar : bars_) {
        if (!bar.frame->isHidden())
            blocks.append(bar.hunkLine);
    }
    return blocks;
}

bool HunkActions::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize)
        placeBars();
    return QObject::eventFilter(watched, event);
}

HunkActions::Bar &HunkActions::barAt(qsizetype index)
{
    while (bars_.size() <= index) {
        Bar bar;
        bar.frame = new QFrame(diff_->viewport());
        bar.frame->setObjectName(QStringLiteral("hunkActions"));
        auto *layout = new QHBoxLayout(bar.frame);
        layout->setContentsMargins(6, 2, 2, 2);
        layout->setSpacing(2);
        bar.claudeMark = new QLabel(tr("✳ Claude"), bar.frame);
        bar.claudeMark->setToolTip(tr("Claude edited this file since it was last committed"));
        AppFonts::setInterfaceFont(bar.claudeMark, 0.9, QFont::Normal);
        layout->addWidget(bar.claudeMark);
        bar.apply = actionButton(bar.frame, tr("Stage"), tr("Stage this block"));
        bar.discard = actionButton(bar.frame, tr("Discard…"), tr("Discard this block (a copy is kept)"));
        bar.ask = actionButton(bar.frame, tr("Ask"), tr("Have the agent's session explain this block"));
        bar.frame->hide();
        // The buttons keep the block they were placed for.
        const qsizetype added = bars_.size();
        connect(bar.apply, &QToolButton::clicked, this, [this, added] { emit applyRequested(bars_[added].hunkLine); });
        connect(bar.discard, &QToolButton::clicked, this,
                [this, added] { emit discardRequested(bars_[added].hunkLine); });
        connect(bar.ask, &QToolButton::clicked, this, [this, added] { emit askRequested(bars_[added].hunkLine); });
        styleBar(bar);
        bars_.append(bar);
    }
    return bars_[index];
}

void HunkActions::styleBar(Bar &bar)
{
    bar.claudeMark->setStyleSheet(QStringLiteral("color: %1").arg(claudeColor_.name()));
    bar.discard->setIcon(discardIcon_);
    bar.ask->setIcon(askIcon_);
    bar.apply->setEnabled(isEnabled_);
    bar.discard->setEnabled(isEnabled_);
}

QList<std::pair<int, int>> HunkActions::blocksInView() const
{
    const QList<DiffLine> &lines = diff_->displayedLines();
    const long firstShown = diff_->firstVisibleLine();
    const int top = int(diff_->SendScintilla(QsciScintillaBase::SCI_DOCLINEFROMVISIBLE, firstShown));
    const int bottom = int(diff_->SendScintilla(QsciScintillaBase::SCI_DOCLINEFROMVISIBLE,
                                                firstShown + diff_->SendScintilla(QsciScintillaBase::SCI_LINESONSCREEN)));
    QList<std::pair<int, int>> blocks;
    // The block at the top, its separator scrolled away: its bar at the top.
    if (const int scrolledAway = diff_->hunkStart(top); scrolledAway >= 0 && scrolledAway < top)
        blocks.append({scrolledAway, 0});
    for (int line = std::max(0, top); line <= bottom && line < lines.size(); ++line) {
        if (lines.at(line).kind == DiffLineKind::HunkHeader)
            blocks.append({line, std::max(0, diff_->lineTop(line))});
    }
    return blocks;
}

void HunkActions::placeBars()
{
    const QList<std::pair<int, int>> blocks = blocksInView();
    for (qsizetype i = 0; i < blocks.size(); ++i)
        placeBar(barAt(i), blocks.at(i).first, blocks.at(i).second);
    for (qsizetype i = blocks.size(); i < bars_.size(); ++i)
        bars_[i].frame->hide();
}

void HunkActions::placeBar(Bar &bar, int hunkLine, int top)
{
    bar.hunkLine = hunkLine;
    const int file = diff_->locate(hunkLine).file;
    updateButtons(bar, stagedFiles_.contains(file));
    bar.claudeMark->setVisible(claudeFiles_.contains(file));
    // Centered on the separator, which it may overlap a little.
    const int lineHeight = std::max(1, diff_->lineHeight());
    bar.frame->adjustSize();
    const int barTop = std::max(0, top + (lineHeight - bar.frame->height()) / 2);
    bar.frame->move(diff_->viewport()->width() - rightMargin - bar.frame->width(), barTop);
    bar.frame->show();
    bar.frame->raise();
}

void HunkActions::updateButtons(Bar &bar, bool isStaged)
{
    bar.apply->setText(isStaged ? tr("Unstage") : tr("Stage"));
    bar.apply->setToolTip(isStaged ? tr("Unstage this block") : tr("Stage this block"));
    bar.apply->setIcon(isStaged ? unstageIcon_ : stageIcon_);
    bar.discard->setVisible(!isStaged);
}
