#pragma once

#include <QColor>
#include <QFont>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

class QPainter;
struct Theme;

// The look of a diagram, ported from sidecar's shape-board: the palette
// (PALETTE), the board options (DEFAULTS: shapes, links, tones, labels,
// ports), text measured and drawn in world units, and the path helpers the
// diagram families draw with. Everything is in world units: the canvas
// draws through a world transform, as sidecar's 2D context does.
namespace diagram {

struct Palette {
    QColor paper;       // canvas
    QColor grid;        // dot grid
    QColor fill;        // default shape body
    QColor ink;         // primary text
    QColor inkMuted;    // secondary text
    QColor border;      // shape edges
    QColor separator;   // inner separators
    QColor labelBg;     // link label body
    QColor labelStroke; // link label outline
    QColor line;        // connectors and heads
    QColor lifeline;    // sequence lifelines
    QColor solid;       // strong / root surfaces
    QColor labelInk;    // link label text
    QColor selection;   // selection accent
    QColor guide;       // alignment guides
    QColor handle;      // handle fill
    QColor shadow;
};

struct Tones {
    bool colorful = true;
    QColor cool;
    QColor sage;
    QColor data;
    QColor warm;
    QColor rose;
    QColor external;
    QColor group;
    QColor strong;
};

struct Style {
    Palette palette;
    QColor background;
    QColor gridColor;
    qreal minScale = 0.15;
    qreal maxScale = 4;
    qreal fitMaxScale = 1.25;
    struct {
        qreal radius = 4;
        QColor fill, stroke, selectedStroke, guide;
    } shape;
    struct {
        QColor color;
        qreal width = 2;
        qreal arrow = 9;
    } link;
    Tones tones;
    struct {
        qreal fontPx = 12, padX = 6, padY = 3;
        QColor bg, stroke, color;
    } linkLabel;
    struct {
        qreal radius = 6;
        QColor fill, stroke;
    } port;
    // True for the light, printable palette of exports.
    bool print = false;

    // On screen, the palette follows the editor's theme: every tone is the
    // paper tinted by the accent, so that the three themes keep sidecar's
    // hierarchy.
    static Style fromTheme(const Theme &theme);
    // sidecar's own light palette, for SVG and image exports.
    static Style printed();
};

// Constants shared by the families (shape-board.js).
constexpr qreal ShapeRadiusSm = 4;
constexpr qreal ShapeBorderWidth = 1;
constexpr qreal LabelGutter = 8;
constexpr qreal ClusterLabelFontPx = 14;
constexpr qreal ClusterMargin = 24;
constexpr qreal LinkWidthMin = 1;
constexpr qreal LinkWidthMax = 8;
// Graphviz `arrowhead=normal`: a base 70 % as wide as the head is long.
extern const qreal GraphvizNormalArrowSpread;
// Distance from an arrow's tip to the middle of its base.
qreal arrowBaseInset(qreal size, qreal spread = GraphvizNormalArrowSpread);

// ---------------------------------------------------------------------------
// Text

// A font in world pixels: what `12.5px Inter` or `italic 600 14px Inter` is
// in sidecar. Text is laid out on a 100 px reference font and scaled, so that
// measures and drawing agree at every zoom.
struct Font {
    qreal px = 14;
    int weight = 400; // CSS weight: 400 regular, 600 semibold, 700 bold
    bool italic = false;
    bool mono = false; // the code font (sidecar's fontMono is its sans font)

    Font() = default;
    constexpr Font(qreal px_, int weight_ = 400, bool italic_ = false, bool mono_ = false)
        : px(px_), weight(weight_), italic(italic_), mono(mono_)
    {
    }
};

enum class Align { Left, Center, Right };
// Canvas text baselines: top of the em box, its middle, the alphabetic
// baseline, the bottom of the em box.
enum class Baseline { Top, Middle, Alphabetic, Bottom };

namespace text {

QFont referenceFont(const Font &font);
qreal width(const Font &font, const QString &text);
qreal ascent(const Font &font);
qreal descent(const Font &font);
// Lines that fit `maxWidth`, honoring explicit newlines; a word longer than
// the width is cut at the longest prefix that fits (wrapText).
QStringList wrap(const Font &font, const QString &text, qreal maxWidth);
// Widest of these lines.
qreal widest(const Font &font, const QStringList &lines);
// fillText: `text` at `position`, aligned and on the baseline given.
void draw(QPainter &painter, const Font &font, const QString &text, QPointF position, const QColor &color,
          Align align = Align::Left, Baseline baseline = Baseline::Alphabetic);
// The ellipsized text that fits `maxWidth`.
QString elided(const Font &font, const QString &text, qreal maxWidth);

} // namespace text

// ---------------------------------------------------------------------------
// Paths and painting

QPainterPath roundRect(qreal x, qreal y, qreal w, qreal h, qreal r);
QPainterPath roundRect(const QRectF &rect, qreal r);
QPainterPath polygon(const QList<QPointF> &points);
QPainterPath ellipse(QPointF center, qreal rx, qreal ry);
// Fills `path` with sidecar's short shadow under it.
void shadowFill(QPainter &painter, const QPainterPath &path, const QColor &fill, const Style &style);
// Strokes `path` in world units; `dash` in world units, as setLineDash().
void stroke(QPainter &painter, const QPainterPath &path, const QColor &color, qreal width,
            const QList<qreal> &dash = {});
// A polyline or curve with an optional dash.
void strokeLine(QPainter &painter, QPointF from, QPointF to, const QColor &color, qreal width,
                const QList<qreal> &dash = {});

// ---------------------------------------------------------------------------
// Colors

// A "#rrggbb" color; invalid if `value` is not one.
QColor parseColor(const QString &value);
bool isHexColor(const QString &value);
QString hexColor(const QColor &color);
// The more legible of `dark` and `light` on `background`.
QColor readableTextColor(const QColor &background, const QColor &dark, const QColor &light = Qt::white);
// `from` moved towards `to` by `amount` (0..1).
QColor mix(const QColor &from, const QColor &to, qreal amount);
QColor withAlpha(const QColor &color, qreal alpha);

// The fills offered for shapes ("#rrggbb"): sidecar's tones, then a few soft
// hues.
const QStringList &swatches();
// The colors offered for links.
const QStringList &lineColors();

} // namespace diagram
