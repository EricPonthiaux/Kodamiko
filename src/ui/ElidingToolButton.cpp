// SPDX-License-Identifier: GPL-3.0-only

#include "ElidingToolButton.h"

#include "UiMetrics.h"

#include <QStyleOptionToolButton>
#include <QStylePainter>

ElidingToolButton::ElidingToolButton(QWidget *parent, int preferredWidth)
    : QToolButton(parent)
    , preferredWidth_(preferredWidth)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

int ElidingToolButton::fullWidth() const
{
    return QToolButton::sizeHint().width() + fontMetrics().horizontalAdvance(suffix_);
}

int ElidingToolButton::boundedWidth() const
{
    return qMax(qMin(fullWidth(), UiMetrics::scaled(preferredWidth_)), minimumSizeHint().width());
}

int ElidingToolButton::widenInto(int sparePx)
{
    const int extra = qBound(0, fullWidth() - boundedWidth(), qMax(0, sparePx));
    if (extra != extraWidth_) {
        extraWidth_ = extra;
        updateGeometry();
    }
    return extra;
}

QSize ElidingToolButton::sizeHint() const
{
    return {qMin(fullWidth(), boundedWidth() + extraWidth_), QToolButton::sizeHint().height()};
}

QSize ElidingToolButton::minimumSizeHint() const
{
    const QSize full = QToolButton::sizeHint();
    const int decoration = full.width() - fontMetrics().horizontalAdvance(text());
    const int minimum = decoration + fontMetrics().horizontalAdvance(QStringLiteral("…") + suffix_);
    return {qMax(UiMetrics::scaled(UiMetrics::toolbarButtonSize + 12), minimum), full.height()};
}

void ElidingToolButton::setTextSuffix(const QString &suffix)
{
    suffix_ = suffix;
    updateGeometry();
    update();
}

void ElidingToolButton::paintEvent(QPaintEvent *)
{
    QStyleOptionToolButton option;
    initStyleOption(&option);
    // Measured as QToolButton::sizeHint() measures it, so that a button at
    // its full width shows the whole label.
    const QFontMetrics metrics = fontMetrics();
    const int textWidth = metrics.size(Qt::TextShowMnemonic, option.text).width();
    const int decoration = QToolButton::sizeHint().width() - textWidth;
    const int available = qMax(0, width() - decoration - metrics.horizontalAdvance(suffix_));
    if (available < textWidth)
        option.text = metrics.elidedText(option.text, Qt::ElideMiddle, available);
    option.text += suffix_;
    QStylePainter painter(this);
    painter.drawComplexControl(QStyle::CC_ToolButton, option);
}
