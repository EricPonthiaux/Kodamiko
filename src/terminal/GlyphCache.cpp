// SPDX-License-Identifier: GPL-3.0-only

#include "GlyphCache.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPaintDevice>
#include <QPainter>

#include <algorithm>

namespace {

// Colors and characters are few in practice: past this, the pictures of an
// old screen are dropped rather than kept forever.
constexpr qsizetype maxPictures = 4096;

QString textOf(const GlyphCache::Key &key)
{
    const auto length = std::find(key.characters.begin(), key.characters.end(), char32_t(0)) - key.characters.begin();
    return QString::fromUcs4(key.characters.data(), length);
}

// `font` for a picture copied onto `target`: a size in points makes as many
// pixels on both, whatever their resolutions.
QFont fontForPicture(const QFont &font, const QPaintDevice &target, const QPaintDevice &picture)
{
    if (font.pointSizeF() <= 0)
        return font;
    QFont scaled = font;
    scaled.setPointSizeF(font.pointSizeF() * target.logicalDpiY() / picture.logicalDpiY());
    return scaled;
}

} // namespace

void GlyphCache::draw(QPainter &painter, QPoint origin, const Key &key, const QFont &font)
{
    const qreal devicePixelRatio = painter.device()->devicePixelRatioF();
    if (devicePixelRatio != devicePixelRatio_) {
        pictures_.clear();
        devicePixelRatio_ = devicePixelRatio;
    }
    auto found = pictures_.constFind(key);
    if (found == pictures_.cend()) {
        if (pictures_.size() >= maxPictures)
            pictures_.clear();
        found = pictures_.insert(key, render(key, font, *painter.device()));
    }
    painter.drawPixmap(origin + found->offset, found->pixmap);
}

void GlyphCache::clear()
{
    pictures_.clear();
}

GlyphCache::Picture GlyphCache::render(const Key &key, const QFont &font, const QPaintDevice &target) const
{
    const QString text = textOf(key);
    const QFontMetricsF metrics(font, &target);
    // The ink, which may overhang the advance, and the advance itself, where
    // underline and strike-out are drawn; one pixel more for antialiasing.
    const QRectF advance(0, -metrics.ascent(), metrics.horizontalAdvance(text), metrics.height());
    const QRect bounds = metrics.boundingRect(text).united(advance).toAlignedRect().adjusted(-1, -1, 1, 1);

    QPixmap pixmap(bounds.size() * devicePixelRatio_);
    pixmap.setDevicePixelRatio(devicePixelRatio_);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setFont(fontForPicture(font, target, pixmap));
    painter.setPen(QColor(key.color));
    painter.drawText(-bounds.topLeft(), text);
    return {.pixmap = pixmap, .offset = bounds.topLeft()};
}

size_t qHash(const GlyphCache::Key &key, size_t seed) noexcept
{
    return qHashMulti(seed, qHashRange(key.characters.begin(), key.characters.end()), key.font, key.color);
}
