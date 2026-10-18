#include "HunkActions.h"

#include "DiffView.h"

#include <QMouseEvent>
#include <QToolButton>

#include <algorithm>

namespace {

constexpr int rightMargin = 8;
constexpr int spacing = 4;

QToolButton *actionButton(QWidget *parent, const QString &text, const QString &toolTip)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("hunkAction"));
    button->setText(text);
    button->setToolTip(toolTip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::ArrowCursor);
    button->hide();
    return button;
}

} // namespace

HunkActions::HunkActions(DiffView *diff)
    : QObject(diff)
    , diff_(diff)
    , apply_(actionButton(diff->viewport(), tr("Stage"), tr("Stage this block")))
    , discard_(actionButton(diff->viewport(), tr("Discard…"), tr("Discard this block (a copy is kept)")))
{
    diff_->viewport()->setMouseTracking(true);
    diff_->viewport()->installEventFilter(this);
    // The buttons keep the block they were shown for.
    connect(apply_, &QToolButton::clicked, this, [this] { emit applyRequested(hunkLine_); });
    connect(discard_, &QToolButton::clicked, this, [this] { emit discardRequested(hunkLine_); });
    connect(diff_, &DiffView::contentChanged, this, &HunkActions::hide);
}

void HunkActions::setStaged(bool staged)
{
    staged_ = staged;
    apply_->setText(staged ? tr("Unstage") : tr("Stage"));
    apply_->setToolTip(staged ? tr("Unstage this block") : tr("Stage this block"));
    hide();
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
        // Onto a button: they stay.
        if (!apply_->underMouse() && !discard_->underMouse())
            hide();
        break;
    case QEvent::Wheel:
        hide(); // the blocks move under the mouse
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
    const int top = std::max(0, diff_->lineTop(hunkLine));
    int right = diff_->viewport()->width() - rightMargin;
    for (QToolButton *button : {discard_, apply_}) {
        const bool shown = button == apply_ || !staged_;
        button->setVisible(shown);
        if (!shown)
            continue;
        button->adjustSize();
        const int height = std::min(button->height(), lineHeight);
        button->setGeometry(right - button->width(), top + (lineHeight - height) / 2, button->width(), height);
        right -= button->width() + spacing;
    }
}

void HunkActions::hide()
{
    apply_->hide();
    discard_->hide();
}
