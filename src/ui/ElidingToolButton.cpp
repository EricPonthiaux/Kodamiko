// SPDX-License-Identifier: GPL-3.0-only

#include "ElidingToolButton.h"

#include "UiMetrics.h"

#include <QPainter>
#include <QStyleOptionToolButton>
#include <QStylePainter>

namespace {

// Added to the 4 pixels the style leaves between an icon and its text,
// which put the text against the icon.
constexpr int extraIconGapPx = 4;

// `icon` at `size`, followed by `marginPx` of transparent pixels: the style
// lays it out and measures it as any icon, so the text moves right.
QIcon withRightMargin(const QIcon &icon, QSize size, int marginPx, qreal pixelRatio)
{
    QPixmap padded((QSizeF(size.width() + marginPx, size.height()) * pixelRatio).toSize());
    padded.setDevicePixelRatio(pixelRatio);
    padded.fill(Qt::transparent);
    QPainter painter(&padded);
    icon.paint(&painter, QRect(QPoint(0, 0), size));
    painter.end();
    return QIcon(padded);
}

} // namespace

ElidingToolButton::ElidingToolButton(QWidget *parent, int preferredWidth)
    : QToolButton(parent)
    , preferredWidth_(preferredWidth)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

int ElidingToolButton::iconGap() const
{
    const bool hasIconBesideText = !icon().isNull() && toolButtonStyle() == Qt::ToolButtonTextBesideIcon;
    return hasIconBesideText ? UiMetrics::scaled(extraIconGapPx) : 0;
}

QSize ElidingToolButton::labelSizeHint() const
{
    return QToolButton::sizeHint() + QSize(iconGap(), 0);
}

int ElidingToolButton::fullWidth() const
{
    return labelSizeHint().width() + fontMetrics().horizontalAdvance(suffix_);
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
    return {qMin(fullWidth(), boundedWidth() + extraWidth_), labelSizeHint().height()};
}

QSize ElidingToolButton::minimumSizeHint() const
{
    const QSize full = labelSizeHint();
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
    const int decoration = labelSizeHint().width() - textWidth;
    const int available = qMax(0, width() - decoration - metrics.horizontalAdvance(suffix_));
    if (available < textWidth)
        option.text = metrics.elidedText(option.text, Qt::ElideMiddle, available);
    option.text += suffix_;
    if (const int gap = iconGap(); gap > 0) {
        option.icon = withRightMargin(option.icon, option.iconSize, gap, devicePixelRatio());
        option.iconSize.rwidth() += gap;
    }
    QStylePainter painter(this);
    painter.drawComplexControl(QStyle::CC_ToolButton, option);
}
