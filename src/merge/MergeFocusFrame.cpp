// SPDX-License-Identifier: GPL-3.0-only

#include "MergeFocusFrame.h"

#include "ui/Motion.h"

#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

namespace {

constexpr int haloMs = 700;
constexpr int haloSpreadPx = 6;
constexpr qreal dimAlpha = 0.35;
constexpr qreal hatchAlpha = 0.22;
constexpr int hatchStepPx = 8;
constexpr int tagPaddingPx = 7;
constexpr int tagMarginPx = 4;

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(alpha);
    return color;
}

} // namespace

MergeFocusFrame::MergeFocusFrame(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , theme_(theme)
    , halo_(new QVariantAnimation(this))
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    halo_->setDuration(haloMs);
    halo_->setStartValue(1.0);
    halo_->setEndValue(0.0);
    halo_->setEasingCurve(QEasingCurve::OutCubic);
    connect(halo_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        haloStrength_ = value.toReal();
        update();
    });
}

void MergeFocusFrame::setBands(const QList<Band> &bands, const QRect &visible)
{
    bands_ = bands;
    visible_ = visible;
    update();
}

void MergeFocusFrame::flash()
{
    if (!Motion::policy().isEnabled())
        return;
    halo_->stop();
    halo_->start();
}

void MergeFocusFrame::applyTheme(const Theme &theme)
{
    theme_ = theme;
    update();
}

void MergeFocusFrame::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setClipRect(visible_);
    painter.setRenderHint(QPainter::Antialiasing);
    // The current band last: its frame and tag over the others'.
    for (const Band &band : std::as_const(bands_)) {
        if (!band.isCurrent)
            paintBand(painter, band);
    }
    for (const Band &band : std::as_const(bands_)) {
        if (band.isCurrent)
            paintBand(painter, band);
    }
}

void MergeFocusFrame::paintBand(QPainter &painter, const Band &band) const
{
    if (!band.area.intersects(visible_))
        return;
    if (band.state != ConflictState::Resolved) {
        // A result not decided yet: its lines are not code to keep.
        painter.save();
        painter.setClipRect(band.result.intersected(visible_));
        painter.setPen(QPen(withAlpha(ConflictStates::color(band.state, theme_), hatchAlpha), 1));
        for (int x = band.result.left() - band.result.height(); x < band.result.right(); x += hatchStepPx)
            painter.drawLine(x, band.result.bottom(), x + band.result.height(), band.result.top());
        painter.restore();
    }
    if (band.isCurrent) {
        const QRectF frame = QRectF(band.area).adjusted(1, 1, -1, -1);
        if (haloStrength_ > 0) {
            const qreal spread = haloSpreadPx * (1 - haloStrength_) + 2;
            painter.setPen(QPen(withAlpha(theme_.foreground, 0.45 * haloStrength_), spread));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(frame.adjusted(-spread / 2, -spread / 2, spread / 2, spread / 2), 3, 3);
        }
        painter.setPen(QPen(theme_.foreground, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(frame, 3, 3);
        paintTag(painter, band);
    } else {
        painter.fillRect(band.area, withAlpha(theme_.background, dimAlpha));
    }
}

// A pill on the band's top edge, over the line numbers: the code stays clear.
void MergeFocusFrame::paintTag(QPainter &painter, const Band &band) const
{
    QFont font = this->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 0.85);
    const QFontMetrics metrics(font);
    const int height = metrics.height() + 2;
    const qreal dot = height * 0.4;
    const int width = metrics.horizontalAdvance(band.tag) + 2 * tagPaddingPx + int(dot) + 2;
    // Inside the band when it starts at the top of the view.
    const int top = std::max(visible_.top() + 2, band.area.top() - height / 2);
    const QRectF pill(band.area.left() + tagMarginPx, top, width, height);
    painter.setPen(QPen(theme_.foreground, 1));
    painter.setBrush(theme_.foreground);
    painter.drawRoundedRect(pill, height / 2.0, height / 2.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ConflictStates::color(band.state, theme_));
    painter.drawEllipse(QRectF(pill.left() + tagPaddingPx - 1, pill.center().y() - dot / 2, dot, dot));
    painter.setFont(font);
    painter.setPen(theme_.window);
    painter.drawText(pill.adjusted(tagPaddingPx + dot + 1, 0, -tagPaddingPx + 2, 0), Qt::AlignCenter, band.tag);
}
