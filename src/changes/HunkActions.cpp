// SPDX-License-Identifier: GPL-3.0-only

#include "HunkActions.h"

#include "DiffView.h"
#include "ui/AppFonts.h"
#include "ui/Theme.h"
#include "ui/UiIcons.h"
#include "ui/UiMetrics.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QToolButton>

#include <algorithm>

namespace {

constexpr int rightMargin = 8;
constexpr int iconSize = 14;

QFrame *actionBar(QWidget *parent)
{
    auto *bar = new QFrame(parent);
    bar->setObjectName(QStringLiteral("hunkActions"));
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    bar->hide();
    return bar;
}

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
    , bar_(actionBar(diff->viewport()))
    , apply_(actionButton(bar_, tr("Stage"), tr("Stage this block")))
    , discard_(actionButton(bar_, tr("Discard…"), tr("Discard this block (a copy is kept)")))
{
    diff_->viewport()->setMouseTracking(true);
    diff_->viewport()->installEventFilter(this);
    // The buttons keep the block they were shown for.
    connect(apply_, &QToolButton::clicked, this, [this] { emit applyRequested(hunkLine_); });
    connect(discard_, &QToolButton::clicked, this, [this] { emit discardRequested(hunkLine_); });
    connect(diff_, &DiffView::contentChanged, this, &HunkActions::hide);
    // However it scrolls (wheel, keys, scroll bar), the blocks move under
    // the buttons: they would act on a block they no longer face.
    connect(diff_, &QsciScintillaBase::SCN_UPDATEUI, this, [this](int updated) {
        if (updated & QsciScintillaBase::SC_UPDATE_V_SCROLL)
            hide();
    });
}

void HunkActions::setStaged(bool staged)
{
    staged_ = staged;
    apply_->setText(staged ? tr("Unstage") : tr("Stage"));
    apply_->setToolTip(staged ? tr("Unstage this block") : tr("Stage this block"));
    apply_->setIcon(staged ? unstageIcon_ : stageIcon_);
    discard_->setVisible(!staged);
    hide();
}

void HunkActions::applyTheme(const Theme &theme)
{
    stageIcon_ = UiIcons::icon(QStringLiteral("plus"), theme.mutedText);
    unstageIcon_ = UiIcons::icon(QStringLiteral("minus"), theme.mutedText);
    apply_->setIcon(staged_ ? unstageIcon_ : stageIcon_);
    discard_->setIcon(UiIcons::icon(QStringLiteral("discard"), theme.mutedText));
}

void HunkActions::setEnabled(bool enabled)
{
    apply_->setEnabled(enabled);
    discard_->setEnabled(enabled);
}

bool HunkActions::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseMove:
        placeAt(static_cast<QMouseEvent *>(event)->position().toPoint().y());
        break;
    case QEvent::Leave:
        // Onto the buttons: they stay.
        if (!bar_->underMouse())
            hide();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void HunkActions::placeAt(int y)
{
    const int lineHeight = std::max(1, diff_->lineHeight());
    const int line = diff_->firstVisibleLine() + y / lineHeight;
    const int hunkLine = diff_->hunkStart(line);
    if (hunkLine < 0) {
        hide();
        return;
    }
    hunkLine_ = hunkLine;
    // Centered on the separator, which it may overlap a little.
    const int top = std::max(0, diff_->lineTop(hunkLine));
    bar_->adjustSize();
    const int barTop = std::max(0, top + (lineHeight - bar_->height()) / 2);
    bar_->move(diff_->viewport()->width() - rightMargin - bar_->width(), barTop);
    bar_->show();
    bar_->raise();
}

void HunkActions::hide()
{
    bar_->hide();
}
