#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

class QPainter;
class QRectF;

// Duet's mark: a thin text caret (the editor) next to a block cursor (the
// terminal where Claude runs). Drawn rather than loaded, so it is sharp at
// every size; packaging/brand holds the same shapes as SVG.
namespace BrandMark {

// The colors of the mark: the caret, then the block.
inline const QColor caretColor{0xe7, 0xe9, 0xef};
inline const QColor blockColor{0x4f, 0x7c, 0xf0};
// The app icon's tile.
inline const QColor tileColor{0x1f, 0x23, 0x2c};

// The two shapes, fitted in the square `area`.
void paint(QPainter *painter, const QRectF &area, const QColor &caret = caretColor,
           const QColor &block = blockColor);
// The app icon in the square `area`: the mark on its rounded dark tile.
void paintAppIcon(QPainter *painter, const QRectF &area);
// The app icon, `size` pixels square.
QPixmap appIcon(int size, qreal devicePixelRatio = 1);
// The app icon at the sizes systems ask for, for the windows.
QIcon windowIcon();

} // namespace BrandMark
