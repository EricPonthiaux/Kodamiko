// SPDX-License-Identifier: GPL-3.0-only

#include "ConflictNavigator.h"

#include "ui/UiIcons.h"
#include "ui/UiMetrics.h"

#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QToolTip>

#include <algorithm>

namespace {

constexpr int dotPx = 20;
constexpr int dotGapPx = 5;
// Around the dots: room for the ring of the one shown.
constexpr int ringMarginPx = 4;
constexpr qreal ringOffsetPx = 2.5;
// Beyond, the dots would be too small to tell apart: the label says it.
constexpr int maxDots = 12;

} // namespace

// A dot per conflict, in its state's color, the one shown ringed; a click
// goes to it.
class ConflictSteps : public QWidget {
    Q_OBJECT

public:
    explicit ConflictSteps(QWidget *parent)
        : QWidget(parent)
    {
        setCursor(Qt::PointingHandCursor);
    }

    void setTheme(const Theme &theme)
    {
        theme_ = theme;
        update();
    }

    void setConflicts(const QList<ConflictState> &states, int current)
    {
        states_ = states;
        current_ = current;
        setVisible(!states.isEmpty() && states.size() <= maxDots);
        setMinimumSize(sizeHint());
        updateGeometry();
        update();
    }

    QSize sizeHint() const override
    {
        const int count = int(states_.size());
        const int margins = 2 * UiMetrics::scaled(ringMarginPx);
        return {count * UiMetrics::scaled(dotPx) + std::max(0, count - 1) * UiMetrics::scaled(dotGapPx) + margins,
                UiMetrics::scaled(dotPx) + margins};
    }

signals:
    void conflictRequested(int index);

protected:
    bool event(QEvent *event) override
    {
        if (event->type() != QEvent::ToolTip)
            return QWidget::event(event);
        const auto *help = static_cast<QHelpEvent *>(event);
        const int index = indexAt(help->pos());
        if (index < 0) {
            QToolTip::hideText();
            return true;
        }
        QToolTip::showText(help->globalPos(),
                           tr("Conflict %1: %2").arg(index + 1).arg(ConflictStates::name(states_[index])));
        return true;
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        for (int i = 0; i < states_.size(); ++i)
            paintDot(painter, i);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        const int index = indexAt(event->position().toPoint());
        if (event->button() == Qt::LeftButton && index >= 0)
            emit conflictRequested(index);
    }

private:
    QRectF dotRect(int index) const
    {
        const int side = UiMetrics::scaled(dotPx);
        const int x = UiMetrics::scaled(ringMarginPx) + index * (side + UiMetrics::scaled(dotGapPx));
        return QRectF(x, (height() - side) / 2.0, side, side).adjusted(1.5, 1.5, -1.5, -1.5);
    }

    int indexAt(QPoint position) const
    {
        for (int i = 0; i < states_.size(); ++i) {
            if (dotRect(i).adjusted(-2, -2, 2, 2).contains(position))
                return i;
        }
        return -1;
    }

    // Open: a ring with the number; resolved: a filled dot with a check.
    void paintDot(QPainter &painter, int index) const
    {
        const QRectF dot = dotRect(index);
        const QColor color = ConflictStates::color(states_[index], theme_);
        const bool isResolved = states_[index] == ConflictState::Resolved;
        if (index == current_) {
            painter.setPen(QPen(theme_.foreground, 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(dot.adjusted(-ringOffsetPx, -ringOffsetPx, ringOffsetPx, ringOffsetPx));
        }
        painter.setPen(QPen(color, 1.5));
        painter.setBrush(isResolved ? color : Qt::transparent);
        painter.drawEllipse(dot);
        QFont font = this->font();
        font.setPixelSize(int(dot.height() * 0.62));
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(isResolved ? theme_.window : theme_.foreground);
        painter.drawText(dot, Qt::AlignCenter, isResolved ? QStringLiteral("✓") : QString::number(index + 1));
    }

    QList<ConflictState> states_;
    int current_ = -1;
    Theme theme_;
};

ConflictNavigator::ConflictNavigator(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , previous_(new QToolButton(this))
    , where_(new QLabel(this))
    , next_(new QToolButton(this))
    , steps_(new ConflictSteps(this))
    , nextOpen_(new QToolButton(this))
{
    previous_->setToolTip(tr("Previous conflict (Shift+F7)"));
    next_->setToolTip(tr("Next conflict (F7)"));
    nextOpen_->setToolTip(tr("Next conflict still to resolve (Alt+F7)"));
    for (QToolButton *button : {previous_, next_, nextOpen_}) {
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        UiMetrics::setScaledIconSize(button);
    }
    where_->setTextFormat(Qt::RichText);
    connect(previous_, &QToolButton::clicked, this, &ConflictNavigator::previousRequested);
    connect(next_, &QToolButton::clicked, this, &ConflictNavigator::nextRequested);
    connect(nextOpen_, &QToolButton::clicked, this, &ConflictNavigator::nextOpenRequested);
    connect(steps_, &ConflictSteps::conflictRequested, this, &ConflictNavigator::conflictRequested);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(previous_);
    layout->addWidget(where_);
    layout->addWidget(next_);
    layout->addSpacing(6);
    layout->addWidget(steps_);
    layout->addSpacing(4);
    layout->addWidget(nextOpen_);
    applyTheme(theme);
    setConflicts({}, -1);
}

void ConflictNavigator::setConflicts(const QList<ConflictState> &states, int current)
{
    const bool hasCurrent = current >= 0 && current < states.size();
    setVisible(!states.isEmpty());
    previous_->setEnabled(states.size() > 1);
    next_->setEnabled(states.size() > 1);
    where_->setText(hasCurrent ? tr("Conflict <b>%1</b> of %2").arg(current + 1).arg(states.size())
                               : tr("%n conflict(s)", nullptr, int(states.size())));
    steps_->setConflicts(states, current);
    const auto open =
        std::ranges::count_if(states, [](ConflictState state) { return state != ConflictState::Resolved; });
    nextOpen_->setText(open == 0 ? tr("All resolved") : tr("%n open", nullptr, int(open)));
    nextOpen_->setEnabled(open > 0);
    nextOpen_->setToolButtonStyle(open == 0 ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonTextOnly);
}

void ConflictNavigator::applyTheme(const Theme &theme)
{
    previous_->setIcon(UiIcons::icon(QStringLiteral("up"), theme.foreground));
    next_->setIcon(UiIcons::icon(QStringLiteral("down"), theme.foreground));
    nextOpen_->setIcon(UiIcons::icon(QStringLiteral("check"), ConflictStates::color(ConflictState::Resolved, theme)));
    steps_->setTheme(theme);
}

#include "ConflictNavigator.moc"
