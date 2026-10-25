// SPDX-License-Identifier: GPL-3.0-only

#include "BrandLogo.h"

#include "AppFonts.h"
#include "BrandMark.h"
#include "UiMetrics.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {

int controlHeight()
{
    return UiMetrics::scaled(UiMetrics::toolbarButtonSize);
}

} // namespace

BrandLogo::BrandLogo(const Theme *theme, QWidget *parent)
    : QWidget(parent)
    , theme_(theme)
{
    setObjectName(QStringLiteral("brandLogo"));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    AppFonts::followApplicationFont(this, [this] {
        setFixedHeight(controlHeight());
        updateGeometry();
    });
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setToolTip(QStringLiteral("Duet"));
}

void BrandLogo::setPlaceWidth(int pixels)
{
    placeWidth_ = pixels;
    updateGeometry();
    update();
}

void BrandLogo::setScale(qreal scale)
{
    Q_ASSERT(scale > 0 && scale <= 1);
    scale_ = scale;
    updateGeometry();
    update();
}

void BrandLogo::setTrailingGap(int pixels)
{
    if (pixels == trailingGap_)
        return;
    trailingGap_ = pixels;
    updateGeometry();
}

QSize BrandLogo::sizeHint() const
{
    const int inkRight = int(std::ceil(BrandMark::inkRect(markArea()).right()));
    return {inkRight + trailingGap_, controlHeight()};
}

void BrandLogo::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    BrandMark::paint(&painter, markArea(), BrandMark::caretColorFor(*theme_));
}

QRectF BrandLogo::markArea() const
{
    const int control = controlHeight();
    const qreal side = control * scale_;
    const int place = std::max(placeWidth_, control);
    return {(place - side) / 2, (control - side) / 2, side, side};
}
