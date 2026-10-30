// SPDX-License-Identifier: GPL-3.0-only

#include "BrandHero.h"

#include "AppFonts.h"
#include "BrandMark.h"

#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr QSize preferredSize{420, 434};
constexpr int smallestWidth = 315;
constexpr qreal sidePadding = 24;
constexpr qreal smallestMarkHeight = 80;
constexpr qreal largestMarkHeight = 144;
constexpr qreal nameGap = 28;
constexpr qreal signatureGap = 12;

struct BrandType {
    QFont name;
    QFont signature;
};

struct BrandLayout {
    QRectF mark;
    QRectF name;
    QRectF signature;
};

QString signature()
{
    return BrandHero::tr("Your editor. Your agent. In sync.");
}

BrandType typeFor(const QFont &base)
{
    QFont name = base;
    name.setPointSizeF(base.pointSizeF() * 3.1);
    name.setWeight(QFont::DemiBold);
    name.setLetterSpacing(QFont::AbsoluteSpacing, -0.65 * base.pointSizeF() / AppFonts::defaultInterfaceSize);
    QFont tagline = base;
    tagline.setPointSizeF(base.pointSizeF() * 1.05);
    tagline.setWeight(QFont::Normal);
    return {.name = name, .signature = tagline};
}

qreal signatureHeight(const QFont &font, qreal width)
{
    return QFontMetricsF(font)
        .boundingRect(QRectF(0, 0, width, 1000), Qt::AlignHCenter | Qt::TextWordWrap, signature())
        .height();
}

BrandLayout layoutOf(const QRectF &area, const BrandType &type)
{
    const QRectF content = area.adjusted(sidePadding, sidePadding, -sidePadding, -sidePadding);
    const qreal nameHeight = QFontMetricsF(type.name).height();
    const qreal taglineHeight = signatureHeight(type.signature, content.width());
    const qreal copyHeight = nameGap + nameHeight + signatureGap + taglineHeight;
    const qreal markHeight =
        std::min({largestMarkHeight, content.width() * 0.42, std::max(qreal(0), content.height() - copyHeight)});
    const qreal top = area.center().y() - (markHeight + copyHeight) / 2;
    const QRectF band(area.center().x() - markHeight / 2, top, markHeight, markHeight);
    const QRectF name(content.left(), band.bottom() + nameGap, content.width(), nameHeight);
    return {.mark = BrandMark::centredArea(band, 1),
            .name = name,
            .signature = QRectF(content.left(), name.bottom() + signatureGap, content.width(), taglineHeight)};
}

// The opacity, out of 255, of the accent at the heart of the mark's halo.
constexpr int haloAlpha = 30;

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

// A soft light of the accent behind the mark, fading out around it.
void paintHalo(QPainter &painter, const QRectF &mark, const QColor &accent)
{
    const QRectF ink = BrandMark::inkRect(mark);
    const qreal radius = ink.height() * 0.85;
    QRadialGradient halo(ink.center(), radius);
    halo.setColorAt(0, withAlpha(accent, haloAlpha));
    halo.setColorAt(1, withAlpha(accent, 0));
    painter.fillRect(ink.adjusted(-radius, -radius, radius, radius), halo);
}

void paintName(QPainter &painter, const QRectF &area, const QFont &font, const Theme &theme)
{
    const QString name = QStringLiteral("Kodamiko");
    const QFontMetricsF metrics(font);
    const qreal cursorWidth = metrics.capHeight() * 0.065;
    const qreal cursorGap = metrics.capHeight() * 0.23;
    const qreal textWidth = metrics.horizontalAdvance(name);
    const qreal left = area.center().x() - (textWidth + cursorGap + cursorWidth) / 2;
    const qreal baseline = area.top() + metrics.ascent();
    painter.setFont(font);
    painter.setPen(theme.foreground);
    painter.drawText(QPointF(left, baseline), name);
    // A still insertion caret signs the wordmark without introducing motion.
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme.accent);
    painter.drawRoundedRect(
        QRectF(left + textWidth + cursorGap, baseline - metrics.capHeight(), cursorWidth, metrics.capHeight()),
        cursorWidth / 2, cursorWidth / 2);
}

} // namespace

BrandHero::BrandHero(Theme theme, QWidget *parent)
    : QWidget(parent)
    , theme_(std::move(theme))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(QStringLiteral("Kodamiko"));
    setAccessibleDescription(signature());
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
    return preferredSize.expandedTo(minimumSizeHint());
}

QSize BrandHero::minimumSizeHint() const
{
    const BrandType type = typeFor(font());
    const QFontMetricsF nameMetrics(type.name);
    const qreal nameWidth = nameMetrics.horizontalAdvance(QStringLiteral("Kodamiko")) + nameMetrics.capHeight() * 0.3;
    const int width = std::max(smallestWidth, int(std::ceil(nameWidth + sidePadding * 2)));
    const qreal copyHeight =
        nameMetrics.height() + signatureGap + signatureHeight(type.signature, width - sidePadding * 2);
    return {width, int(std::ceil(sidePadding * 2 + smallestMarkHeight + nameGap + copyHeight))};
}

void BrandHero::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const BrandType type = typeFor(font());
    const BrandLayout layout = layoutOf(rect(), type);
    paintHalo(painter, layout.mark, theme_.accent);
    BrandMark::paint(&painter, layout.mark, BrandMark::colorsFor(theme_));
    paintName(painter, layout.name, type.name, theme_);
    painter.setFont(type.signature);
    painter.setPen(theme_.mutedText);
    painter.drawText(layout.signature, Qt::AlignHCenter | Qt::TextWordWrap, signature());
}
