// SPDX-License-Identifier: GPL-3.0-only

#include "BrandHero.h"

#include "AppFonts.h"
#include "ThemeStyle.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

namespace {

constexpr qreal topPadding = 24;
constexpr qreal sidePadding = 28;
constexpr qreal bottomPadding = 20;
constexpr qreal copyGap = 10;      // between the headline and the line below it
constexpr qreal signatureGap = 16; // above the signature
constexpr qreal artGap = 6;        // around the artwork
constexpr qreal minimumArtHeight = 120;
constexpr QSize preferredSize{420, 434};

// The artwork is drawn in the units of the mockup, where it is 276 high;
// the sculpture is 150 × 200 in them, its caret 35 wide, its block 80 × 140.
constexpr qreal artUnits = 276;
constexpr qreal sculptureTop = 31;
constexpr QSizeF caretSize{35, 200};
constexpr QSizeF blockSize{80, 140};
constexpr qreal caretToBlock = 35;
constexpr qreal largestScale = 1.2;

// The orbits, in the units of the mockup's 440 × 290 view box, turned
// together around its centre.
constexpr QSizeF orbitBox{440, 290};
constexpr QPointF orbitCentre{220, 164};
constexpr qreal orbitTilt = -27;
constexpr qreal fieldTilt = -19;
constexpr std::array<QSizeF, 3> orbitRadii{{{173, 60}, {205, 87}, {238, 116}}};
constexpr std::array<QPointF, 2> orbitPlanets{{{65, 230}, {383, 88}}};
constexpr qreal planetRadius = 3;

constexpr qreal starSpacing = 19;
constexpr qreal starRadius = 0.7;
constexpr qreal starFade = 0.75; // of the way to the corners, where the stars are gone

// Opacities, out of 255, of what is drawn in the theme's colors.
constexpr int starAlpha = 56;
constexpr int tintAlpha = 22;
constexpr int glowAlpha = 60;
constexpr int blockGlowAlpha = 54;
constexpr int orbitAlpha = 96;
constexpr int sparkleAlpha = 150;
constexpr int captionAlpha = 200;

struct HeroFonts {
    QFont label; // the small capitals in a monospace font
    QFont headline;
    QFont subtitle;
};

HeroFonts fontsFor(const QFont &base)
{
    QFont label = AppFonts::codeFont(base.pointSizeF() * 0.72);
    label.setLetterSpacing(QFont::AbsoluteSpacing, 1.6);
    QFont headline = base;
    headline.setPointSizeF(base.pointSizeF() * 2.35);
    headline.setWeight(QFont::Normal);
    headline.setLetterSpacing(QFont::PercentageSpacing, 96);
    QFont subtitle = base;
    subtitle.setPointSizeF(base.pointSizeF() * 0.95);
    subtitle.setWeight(QFont::Normal);
    return {.label = label, .headline = headline, .subtitle = subtitle};
}

// Where the hero's parts go, from the bottom up: the artwork takes what
// the text leaves.
struct HeroLayout {
    QRectF overline;
    QRectF art;
    QRectF headline;
    QRectF subtitle;
    QRectF signature;
};

HeroLayout layoutOf(const QRectF &area, const HeroFonts &fonts)
{
    const QRectF content = area.adjusted(sidePadding, topPadding, -sidePadding, -bottomPadding);
    const qreal labelHeight = QFontMetricsF(fonts.label).height();
    const qreal headlineHeight = 2 * QFontMetricsF(fonts.headline).lineSpacing();
    const qreal subtitleHeight = 2 * QFontMetricsF(fonts.subtitle).lineSpacing();
    const QRectF signature(content.left(), content.bottom() - labelHeight, content.width(), labelHeight);
    const QRectF subtitle(content.left(), signature.top() - signatureGap - subtitleHeight, content.width(),
                          subtitleHeight);
    const QRectF headline(content.left(), subtitle.top() - copyGap - headlineHeight, content.width(), headlineHeight);
    const QRectF overline(content.left(), content.top(), content.width(), labelHeight);
    // The artwork spans the whole width: its orbits go beyond the margins.
    const QRectF art(area.left(), overline.bottom() + artGap, area.width(),
                     headline.top() - artGap - overline.bottom() - artGap);
    return {.overline = overline, .art = art, .headline = headline, .subtitle = subtitle, .signature = signature};
}

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

// A wide tint of the accent behind the stars, then the stars, fading out
// towards the corners.
void paintField(QPainter &painter, const QRectF &area, const Theme &theme)
{
    QRadialGradient tint(QPointF(area.center().x(), area.height() * 0.45), area.width() * 0.6);
    tint.setColorAt(0, withAlpha(theme.accent, tintAlpha));
    tint.setColorAt(1, withAlpha(theme.accent, 0));
    painter.fillRect(area, tint);

    painter.setPen(Qt::NoPen);
    const QPointF centre = area.center();
    const qreal halfWidth = area.width() / 2;
    const qreal halfHeight = area.height() / 2;
    const int rows = int(area.height() / starSpacing);
    const int columns = int(area.width() / starSpacing);
    for (int row = 0; row <= rows; ++row) {
        for (int column = 0; column <= columns; ++column) {
            const QPointF star((column + 0.5) * starSpacing, (row + 0.5) * starSpacing);
            const qreal dx = (star.x() - centre.x()) / halfWidth;
            const qreal dy = (star.y() - centre.y()) / halfHeight;
            const qreal distance = std::hypot(dx, dy) / std::numbers::sqrt2;
            const qreal fade = 1 - distance / starFade;
            if (fade <= 0)
                continue;
            painter.setBrush(withAlpha(theme.foreground, int(starAlpha * fade)));
            painter.drawEllipse(star, starRadius, starRadius);
        }
    }
}

// The view box of the orbits, fitted and centred in `art`, turned.
QTransform orbitTransform(const QRectF &art)
{
    const qreal scale = std::min(art.width() / orbitBox.width(), art.height() / orbitBox.height());
    QTransform transform;
    transform.translate(art.center().x(), art.center().y());
    transform.scale(scale, scale);
    transform.rotate(fieldTilt);
    transform.translate(-orbitBox.width() / 2, -orbitBox.height() / 2);
    return transform;
}

void paintOrbits(QPainter &painter, const QRectF &art, const Theme &theme)
{
    const QTransform field = orbitTransform(art);
    QTransform tilt;
    tilt.translate(orbitCentre.x(), orbitCentre.y());
    tilt.rotate(orbitTilt);
    const QColor color = withAlpha(theme.accent, orbitAlpha);
    painter.save();
    painter.setTransform(tilt * field);
    painter.setPen(QPen(color, 1.0 / field.m11(), Qt::SolidLine));
    painter.setBrush(Qt::NoBrush);
    for (const QSizeF &radii : orbitRadii)
        painter.drawEllipse(QPointF(0, 0), radii.width(), radii.height());
    painter.setTransform(field);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    for (const QPointF &planet : orbitPlanets)
        painter.drawEllipse(planet, planetRadius, planetRadius);
    painter.restore();
}

// The light the mark stands in, under and right of its centre.
void paintGlow(QPainter &painter, const QRectF &art, const Theme &theme)
{
    const QPointF centre(art.left() + art.width() * 0.57, art.top() + art.height() * 0.63);
    const qreal radius = art.width() * 0.42;
    QRadialGradient glow(centre, radius);
    glow.setColorAt(0, withAlpha(theme.accent, glowAlpha));
    glow.setColorAt(1, withAlpha(theme.accent, 0));
    painter.save();
    // An ellipse, flatter than it is wide, as the mockup's.
    painter.translate(centre);
    painter.scale(1, art.height() / art.width() * 1.4);
    painter.translate(-centre);
    painter.fillRect(QRectF(centre.x() - radius, centre.y() - radius, 2 * radius, 2 * radius), glow);
    painter.restore();
}

// The sculpture's two shapes, in the units of the mockup scaled by `scale`.
struct Sculpture {
    QRectF caret;
    QRectF block;
};

Sculpture sculptureIn(const QRectF &art, qreal scale)
{
    const qreal width = (caretSize.width() + caretToBlock + blockSize.width()) * scale;
    const qreal left = art.center().x() - width / 2;
    const qreal bottom = art.top() + (sculptureTop + caretSize.height()) * scale;
    const QRectF caret(left, bottom - caretSize.height() * scale, caretSize.width() * scale,
                       caretSize.height() * scale);
    const QRectF block(caret.right() + caretToBlock * scale, bottom - blockSize.height() * scale,
                       blockSize.width() * scale, blockSize.height() * scale);
    return {.caret = caret, .block = block};
}

void paintSculpture(QPainter &painter, const Sculpture &sculpture, const Theme &theme)
{
    // The block's own light, before it.
    const QRectF block = sculpture.block;
    QRadialGradient halo(block.center(), block.height() * 0.85);
    halo.setColorAt(0, withAlpha(theme.accent, blockGlowAlpha));
    halo.setColorAt(1, withAlpha(theme.accent, 0));
    painter.fillRect(block.adjusted(-block.height(), -block.height(), block.height(), block.height()), halo);

    // Lit from the left, as a polished bar.
    QLinearGradient caret(sculpture.caret.topLeft(), sculpture.caret.topRight());
    caret.setColorAt(0, ThemeStyle::mixed(theme.window, theme.foreground, 0.55));
    caret.setColorAt(0.45, theme.foreground);
    caret.setColorAt(0.75, ThemeStyle::mixed(theme.window, theme.foreground, 0.8));
    caret.setColorAt(1, ThemeStyle::mixed(theme.window, theme.foreground, 0.45));
    painter.setPen(Qt::NoPen);
    painter.setBrush(caret);
    const qreal caretRadius = sculpture.caret.width() / 2;
    painter.drawRoundedRect(sculpture.caret, caretRadius, caretRadius);

    QLinearGradient fill(block.topLeft(), block.bottomRight());
    fill.setColorAt(0, ThemeStyle::mixed(theme.accent, theme.foreground, 0.45));
    fill.setColorAt(0.6, theme.accent);
    fill.setColorAt(1, ThemeStyle::mixed(theme.accent, theme.window, 0.5));
    painter.setBrush(fill);
    const qreal blockRadius = block.width() / 4;
    painter.drawRoundedRect(block, blockRadius, blockRadius);
}

// A label with a dot before it, its left at `position`.
void paintMarker(QPainter &painter, QPointF position, const QString &text, const QColor &color)
{
    constexpr qreal dotSize = 4;
    constexpr qreal dotGap = 6;
    const QFontMetricsF metrics(painter.font());
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QRectF(position.x(), position.y() - dotSize / 2, dotSize, dotSize));
    painter.setPen(color);
    painter.drawText(QPointF(position.x() + dotSize + dotGap, position.y() + metrics.capHeight() / 2), text);
}

// Who each shape is, in the painter's font.
void paintMarkers(QPainter &painter, const QRectF &art, qreal scale, const Theme &theme)
{
    const qreal centre = art.center().x();
    paintMarker(painter, QPointF(centre - 128 * scale, art.top() + 107 * scale), QObject::tr("You"), theme.mutedText);
    paintMarker(painter, QPointF(centre + 95 * scale, art.top() + 180 * scale), QObject::tr("Claude"), theme.accent);
}

void paintSparkles(QPainter &painter, const QRectF &art, qreal scale, const Theme &theme)
{
    painter.save();
    painter.setFont(AppFonts::codeFont(painter.font().pointSizeF() * 1.6));
    painter.setPen(withAlpha(theme.accent, sparkleAlpha));
    const QString sparkle = QStringLiteral("+");
    painter.drawText(QPointF(art.left() + art.width() * 0.16, art.top() + 40 * scale), sparkle);
    painter.drawText(QPointF(art.right() - art.width() * 0.14, art.top() + 72 * scale), sparkle);
    painter.drawText(QPointF(art.right() - art.width() * 0.23, art.bottom() - 32 * scale), sparkle);
    painter.restore();
}

void paintCaption(QPainter &painter, const QRectF &art, const Theme &theme)
{
    painter.setPen(withAlpha(theme.mutedText, captionAlpha));
    painter.drawText(art.adjusted(0, 0, 0, -2), Qt::AlignHCenter | Qt::AlignBottom,
                     QObject::tr("TWO CURSORS. ONE FLOW."));
}

void paintCopy(QPainter &painter, const HeroLayout &layout, const HeroFonts &fonts, const Theme &theme)
{
    painter.setFont(fonts.label);
    painter.setPen(theme.mutedText);
    painter.drawText(layout.overline, Qt::AlignCenter, QObject::tr("A SPACE FOR WHAT COMES NEXT"));

    const qreal headlineLine = QFontMetricsF(fonts.headline).lineSpacing();
    const QRectF first = layout.headline.adjusted(0, 0, 0, -headlineLine);
    painter.setFont(fonts.headline);
    painter.setPen(theme.foreground);
    painter.drawText(first, Qt::AlignCenter, QObject::tr("Small spark."));
    painter.setPen(theme.accent);
    painter.drawText(first.translated(0, headlineLine), Qt::AlignCenter, QObject::tr("Limitless ideas."));

    painter.setFont(fonts.subtitle);
    painter.setPen(theme.mutedText);
    painter.drawText(layout.subtitle, Qt::AlignCenter,
                     QObject::tr("One shared view. A world of ideas.\nLet’s see where yours takes you."));

    // A short line of the accent, then the signature.
    painter.setFont(fonts.label);
    const QString signature = QObject::tr("YOU BRING THE SPARK.");
    constexpr qreal lineWidth = 24;
    constexpr qreal lineGap = 10;
    const qreal textWidth = QFontMetricsF(fonts.label).horizontalAdvance(signature);
    const qreal left = layout.signature.center().x() - (lineWidth + lineGap + textWidth) / 2;
    const qreal middle = layout.signature.center().y();
    painter.fillRect(QRectF(left, middle, lineWidth, 1), theme.accent);
    painter.drawText(
        QRectF(left + lineWidth + lineGap, layout.signature.top(), textWidth + 2, layout.signature.height()),
        Qt::AlignLeft | Qt::AlignVCenter, signature);
}

} // namespace

BrandHero::BrandHero(Theme theme, QWidget *parent)
    : QWidget(parent)
    , theme_(std::move(theme))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(tr("You and Claude. Two cursors, one flow."));
    AppFonts::followApplicationFont(this, [this] {
        updateGeometry();
        update();
    });
}

void BrandHero::applyTheme(const Theme &theme)
{
    theme_ = theme;
    update();
}

QSize BrandHero::sizeHint() const
{
    return preferredSize;
}

QSize BrandHero::minimumSizeHint() const
{
    const HeroFonts fonts = fontsFor(font());
    const HeroLayout layout = layoutOf(QRectF(0, 0, preferredSize.width(), preferredSize.height()), fonts);
    const qreal text = preferredSize.height() - layout.art.height();
    return {preferredSize.width() * 3 / 4, int(std::ceil(text + minimumArtHeight))};
}

void BrandHero::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const QRectF area = rect();
    const HeroFonts fonts = fontsFor(font());
    const HeroLayout layout = layoutOf(area, fonts);
    const qreal scale = std::min(layout.art.height() / artUnits, largestScale);

    paintField(painter, area, theme_);
    paintGlow(painter, layout.art, theme_);
    paintOrbits(painter, layout.art, theme_);
    paintSculpture(painter, sculptureIn(layout.art, scale), theme_);
    painter.setFont(fonts.label);
    paintMarkers(painter, layout.art, scale, theme_);
    paintSparkles(painter, layout.art, scale, theme_);
    paintCaption(painter, layout.art, theme_);
    paintCopy(painter, layout, fonts, theme_);
}
