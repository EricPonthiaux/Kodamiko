// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QHash>
#include <QPixmap>
#include <QPoint>
#include <QRgb>

#include <vterm.h>

#include <array>

class QFont;
class QPaintDevice;
class QPainter;

// Pictures of the characters a terminal draws alone in their cell (box
// drawing the font draws wider than a cell, symbols from a fallback font,
// emoji, CJK). Shaping a character and finding its font costs far more than
// copying a picture, and a program such as Claude Code redraws the same few
// characters (frames, spinners, bullets) on every frame.
class GlyphCache {
public:
    // The characters of one cell, in a font of the terminal and a color.
    struct Key {
        std::array<char32_t, VTERM_MAX_CHARS_PER_CELL> characters{};
        int font = 0; // which font of the terminal: the pictures are dropped with clear() when they change
        QRgb color = 0;

        bool operator==(const Key &) const = default;
    };

    // Draws `key`'s characters as painter.drawText(origin, …) would, in
    // `font`, the font `key.font` names.
    void draw(QPainter &painter, QPoint origin, const Key &key, const QFont &font);
    // Forgets every picture: the fonts changed.
    void clear();

private:
    struct Picture {
        QPixmap pixmap;
        QPoint offset; // from the baseline's origin to the picture's top left
    };

    Picture render(const Key &key, const QFont &font, const QPaintDevice &target) const;

    QHash<Key, Picture> pictures_;
    qreal devicePixelRatio_ = 1;
};

size_t qHash(const GlyphCache::Key &key, size_t seed = 0) noexcept;
