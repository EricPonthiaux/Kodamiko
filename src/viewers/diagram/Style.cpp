// SPDX-License-Identifier: GPL-3.0-only

#include "Style.h"

#include "ui/AppFonts.h"
#include "ui/Theme.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace diagram {

// Graphviz draws its half-width as 0.35 times the head's length (arrows.c,
// arrow_type_normal): the sides' angle to the axis is atan(0.35).
const qreal graphvizNormalArrowSpread = std::atan(0.35);

qreal arrowBaseInset(qreal size, qreal spread)
{
    return size * std::cos(spread);
}

namespace {

QColor rgb(uint32_t hex)
{
    return {int((hex >> 16) & 0xff), int((hex >> 8) & 0xff), int(hex & 0xff)};
}

qreal channelLuminance(qreal v)
{
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

qreal luminance(const QColor &color)
{
    return 0.2126 * channelLuminance(color.redF()) + 0.7152 * channelLuminance(color.greenF()) +
           0.0722 * channelLuminance(color.blueF());
}

QColor contrastingColor(const QColor &seed, const QColor &paper, qreal minimum)
{
    const qreal background = luminance(paper);
    const QColor toward = readableTextColor(paper, Qt::black);
    for (int step = 0; step <= 20; ++step) {
        const QColor color = mix(seed, toward, step / 20.0);
        const qreal foreground = luminance(color);
        const qreal contrast = (std::max(background, foreground) + 0.05) / (std::min(background, foreground) + 0.05);
        if (contrast >= minimum)
            return color;
    }
    return toward;
}

QColor syntaxColor(const Theme &theme, QStringView capture)
{
    const int index = theme.syntaxStyleIndex(capture);
    return index >= 0 ? theme.syntaxStyles[index].second.foreground : theme.accent;
}

void mindmapColors(Style &style, const Theme &theme)
{
    const std::array seeds{theme.accent,
                           syntaxColor(theme, u"type"),
                           syntaxColor(theme, u"keyword"),
                           syntaxColor(theme, u"number"),
                           syntaxColor(theme, u"string"),
                           syntaxColor(theme, u"function")};
    for (size_t i = 0; i < seeds.size(); ++i)
        style.mindmap.branches[i] = contrastingColor(seeds[i], theme.background, 3);
    style.mindmap.rootStroke = style.mindmap.branches.front();
    style.mindmap.fillTint = theme.isDark ? 0.18 : 0.07;
    style.mindmap.rootFill = mix(theme.background, style.mindmap.rootStroke, theme.isDark ? 0.24 : 0.09);
}

void finish(Style &style)
{
    const Palette &p = style.palette;
    style.background = p.paper;
    style.gridColor = p.grid;
    style.shape.fill = p.fill;
    style.shape.stroke = p.border;
    style.shape.selectedStroke = p.selection;
    style.shape.guide = p.guide;
    style.link.color = p.line;
    style.linkLabel.bg = p.labelBg;
    style.linkLabel.stroke = p.labelStroke;
    style.linkLabel.color = p.labelInk;
    style.port.fill = p.handle;
    style.port.stroke = p.border;
}

void classifierColors(Style &style, const Theme &theme)
{
    auto &colors = style.classifier;
    colors.fill = mix(theme.background, theme.foreground, theme.isDark ? 0.04 : 0.015);
    colors.header = mix(theme.background, theme.accent, theme.isDark ? 0.19 : 0.075);
    colors.stroke = contrastingColor(mix(theme.background, theme.foreground, 0.38), colors.fill, 3);
    colors.separator = mix(colors.fill, colors.stroke, 0.55);
    colors.line = contrastingColor(mix(theme.background, theme.foreground, 0.64), theme.background, 3);
    colors.ink = theme.foreground;
    colors.mutedInk = contrastingColor(mix(theme.background, theme.foreground, 0.78), colors.header, 4.5);
}

void sequenceColors(Style &style, const Theme &theme)
{
    auto &colors = style.sequence;
    colors.header = mix(theme.background, theme.accent, theme.isDark ? 0.19 : 0.075);
    colors.fragment = mix(theme.background, theme.foreground, theme.isDark ? 0.018 : 0.008);
    colors.stroke = contrastingColor(mix(theme.background, theme.foreground, 0.5), colors.header, 3);
    colors.lifeline = mix(theme.background, theme.foreground, 0.36);
    colors.line = contrastingColor(mix(theme.background, theme.foreground, 0.64), colors.fragment, 3);
    colors.fragmentStroke = mix(theme.background, theme.foreground, 0.4);
    colors.ink = theme.foreground;
    colors.mutedInk = contrastingColor(mix(theme.background, theme.foreground, 0.78), colors.fragment, 4.5);
}

void stateColors(Style &style, const Theme &theme)
{
    auto &colors = style.state;
    colors.fill = mix(theme.background, theme.foreground, theme.isDark ? 0.035 : 0.015);
    colors.header = mix(theme.background, theme.accent, theme.isDark ? 0.19 : 0.075);
    colors.stroke = contrastingColor(mix(theme.background, theme.foreground, 0.5), colors.header, 3);
    colors.separator = mix(colors.fill, colors.stroke, 0.55);
    colors.container = mix(theme.background, theme.foreground, theme.isDark ? 0.018 : 0.008);
    colors.containerHeader = mix(theme.background, theme.accent, theme.isDark ? 0.10 : 0.035);
    colors.line = contrastingColor(mix(theme.background, theme.foreground, 0.64), colors.container, 3);
    colors.marker = contrastingColor(mix(theme.background, theme.foreground, 0.85), colors.container, 3);
    colors.ink = theme.foreground;
    colors.mutedInk = contrastingColor(mix(theme.background, theme.foreground, 0.78), colors.fill, 4.5);
}

} // namespace

Style Style::printed()
{
    Style style;
    style.print = true;
    Palette &p = style.palette;
    p.paper = rgb(0xfafafa);
    p.grid = rgb(0xe0e0e0);
    p.fill = rgb(0xe8eaf6);
    p.ink = rgb(0x1a237e);
    p.inkMuted = rgb(0x303f9f);
    p.border = rgb(0x7986cb);
    p.separator = rgb(0xc5cae9);
    p.labelBg = rgb(0xfafafa);
    p.labelStroke = rgb(0xc5cae9);
    p.line = rgb(0x5c6bc0);
    p.lifeline = rgb(0x7986cb);
    p.solid = rgb(0x303f9f);
    p.labelInk = rgb(0x303f9f);
    p.selection = rgb(0x3949ab);
    p.guide = rgb(0x9fa8da);
    p.handle = Qt::white;
    p.shadow = QColor(0, 0, 0, 31);
    Tones &t = style.tones;
    t.cool = rgb(0xc5cae9);
    t.sage = rgb(0xe8eaf6);
    t.data = rgb(0xb2ebf2);
    t.warm = rgb(0xe0f7fa);
    t.rose = rgb(0xb2ebf2);
    t.external = rgb(0xeeeeee);
    t.group = rgb(0xe8eaf6);
    t.strong = p.solid;
    style.mindmap.branches = {rgb(0x315f9e), rgb(0x296958), rgb(0x72539f), rgb(0x875b1d), rgb(0x487239), rgb(0x9b526f)};
    style.mindmap.rootStroke = style.mindmap.branches.front();
    style.mindmap.rootFill = mix(p.paper, style.mindmap.rootStroke, 0.07);
    style.mindmap.fillTint = 0.06;
    style.classifier = {Qt::white,     rgb(0xedf3fc), rgb(0x8391a5), rgb(0xcfd8e4),
                        rgb(0x62778e), rgb(0x233147), rgb(0x52637a)};
    style.sequence = {rgb(0xedf3fc), rgb(0x8391a5), rgb(0xaab4c3), rgb(0x62778e),
                      rgb(0xf5f7fa), rgb(0xaab4c3), rgb(0x233147), rgb(0x52637a)};
    style.state = {Qt::white,     rgb(0xedf3fc), rgb(0x8391a5), rgb(0xcfd8e4), rgb(0xf5f7fa),
                   rgb(0xedf3fc), rgb(0x62778e), rgb(0x52637a), rgb(0x233147), rgb(0x52637a)};
    finish(style);
    return style;
}

Style Style::fromTheme(const Theme &theme)
{
    // sidecar's palette is indigo on paper with cyan for data and decisions:
    // here the indigo is the theme's accent, the paper its background.
    const QColor paper = theme.background;
    const QColor ink = theme.foreground;
    const QColor accent = theme.accent;
    const QColor cyan(0x26, 0xc6, 0xda);
    const qreal tint = theme.isDark ? 1.4 : 1.0;
    Style style;
    Palette &p = style.palette;
    p.paper = paper;
    p.grid = mix(paper, ink, theme.isDark ? 0.16 : 0.14);
    p.fill = mix(paper, accent, 0.10 * tint);
    p.ink = ink;
    p.inkMuted = mix(paper, ink, 0.72);
    p.border = mix(paper, accent, 0.6);
    p.separator = mix(paper, accent, 0.3);
    p.labelBg = mix(paper, ink, 0.04);
    p.labelStroke = mix(paper, accent, 0.35);
    p.line = mix(paper, accent, 0.8);
    p.lifeline = mix(paper, accent, 0.6);
    p.solid = mix(paper, accent, theme.isDark ? 0.62 : 0.85);
    p.labelInk = mix(paper, ink, 0.85);
    p.selection = accent;
    p.guide = mix(paper, accent, 0.45);
    p.handle = paper;
    p.shadow = QColor(0, 0, 0, theme.isDark ? 70 : 31);
    Tones &t = style.tones;
    t.cool = mix(paper, accent, 0.24 * tint);
    t.sage = mix(paper, accent, 0.10 * tint);
    t.data = mix(paper, cyan, 0.26 * tint);
    t.warm = mix(paper, cyan, 0.13 * tint);
    t.rose = mix(paper, cyan, 0.26 * tint);
    t.external = mix(paper, ink, 0.10);
    t.group = mix(paper, accent, 0.10 * tint);
    t.strong = p.solid;
    mindmapColors(style, theme);
    classifierColors(style, theme);
    sequenceColors(style, theme);
    stateColors(style, theme);
    finish(style);
    return style;
}

// ---------------------------------------------------------------------------
// Text

namespace text {

namespace {

constexpr qreal referencePx = 100;

const QFontMetricsF &metrics(const Font &font)
{
    // By value: an unordered_map keeps its elements in place as it grows.
    static std::unordered_map<int, QFontMetricsF> cache;
    const int key = font.weight * 4 + (font.italic ? 2 : 0) + (font.mono ? 1 : 0);
    auto it = cache.find(key);
    if (it == cache.end())
        it = cache.try_emplace(key, referenceFont(font)).first;
    return it->second;
}

} // namespace

QFont referenceFont(const Font &font)
{
    QFont result = font.mono ? AppFonts::codeFont(12) : AppFonts::interfaceFont();
    result.setPixelSize(int(referencePx));
    result.setWeight(QFont::Weight(std::clamp(font.weight, 100, 900)));
    result.setItalic(font.italic);
    result.setHintingPreference(QFont::PreferNoHinting);
    result.setKerning(true);
    return result;
}

qreal width(const Font &font, const QString &value)
{
    if (value.isEmpty())
        return 0;
    return metrics(font).horizontalAdvance(value) * font.px / referencePx;
}

qreal ascent(const Font &font)
{
    return metrics(font).ascent() * font.px / referencePx;
}

qreal descent(const Font &font)
{
    return metrics(font).descent() * font.px / referencePx;
}

namespace {

QString joined(const QString &line, const QString &token)
{
    return line.isEmpty() ? token : line + QLatin1Char(' ') + token;
}

// The length of the longest prefix of `token` that fits `maxWidth`; at least
// one character, so that cutting always progresses, and never half of a
// surrogate pair (an emoji), which would draw as two boxes.
int fittingPrefixLength(const Font &font, const QString &token, qreal maxWidth)
{
    int lo = 1;
    int hi = int(token.size());
    int fit = 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (width(font, token.left(mid)) <= maxWidth) {
            fit = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    const bool splitsAPair = fit < token.size() && token.at(fit - 1).isHighSurrogate();
    if (splitsAPair)
        fit = fit > 1 ? fit - 1 : fit + 1;
    return fit;
}

// Fills lines word by word, the way wrapText does.
class LineWrapper {
public:
    LineWrapper(const Font &font, qreal maxWidth)
        : font_(font)
        , maxWidth_(maxWidth)
    {
    }

    void addParagraph(const QString &paragraph)
    {
        static const QRegularExpression spaces(QStringLiteral("\\s+"));
        // A blank line, spaces or a CRLF's "\r" included, is kept.
        if (paragraph.trimmed().isEmpty()) {
            lines_.append(QString());
            return;
        }
        line_.clear();
        for (const QString &token : paragraph.trimmed().split(spaces, Qt::SkipEmptyParts))
            addToken(token);
        if (!line_.isEmpty())
            lines_.append(line_);
    }

    QStringList lines() const { return lines_; }

private:
    void addToken(const QString &token)
    {
        const bool overflows = width(font_, joined(line_, token)) > maxWidth_;
        if (overflows && !line_.isEmpty()) {
            lines_.append(line_);
            line_.clear();
        }
        if (width(font_, token) > maxWidth_)
            cutToken(token);
        else
            line_ = joined(line_, token);
    }

    // An identifier or a URL may hold no space: it is cut at the longest
    // prefix that really fits instead of overflowing. The last piece stays
    // open for the next word.
    void cutToken(const QString &token)
    {
        QString rest = token;
        while (!rest.isEmpty()) {
            const int fit = fittingPrefixLength(font_, rest, maxWidth_);
            const QString piece = rest.left(fit);
            rest = rest.mid(fit);
            if (!rest.isEmpty())
                lines_.append(piece);
            else
                line_ = piece;
        }
    }

    Font font_;
    qreal maxWidth_;
    QStringList lines_;
    QString line_;
};

} // namespace

QStringList wrap(const Font &font, const QString &value, qreal maxWidth)
{
    LineWrapper wrapper(font, maxWidth);
    for (const QString &paragraph : value.split(QLatin1Char('\n')))
        wrapper.addParagraph(paragraph);
    return wrapper.lines();
}

qreal widest(const Font &font, const QStringList &lines)
{
    qreal result = 0;
    for (const QString &line : lines)
        result = std::max(result, width(font, line));
    return result;
}

// NOLINTNEXTLINE(readability-function-size): mirrors canvas fillText, called across the families
void draw(QPainter &painter,
          const Font &font,
          const QString &value,
          QPointF position,
          const QColor &color,
          Align align,
          Baseline baseline)
{
    if (value.isEmpty())
        return;
    const qreal scale = font.px / referencePx;
    const QFontMetricsF &m = metrics(font);
    qreal x = 0;
    if (align != Align::Left) {
        const qreal advance = m.horizontalAdvance(value);
        x = align == Align::Center ? -advance / 2 : -advance;
    }
    qreal y = 0;
    switch (baseline) {
    case Baseline::Top:
        y = m.ascent();
        break;
    case Baseline::Middle:
        y = (m.ascent() - m.descent()) / 2;
        break;
    case Baseline::Alphabetic:
        break;
    case Baseline::Bottom:
        y = -m.descent();
        break;
    }
    painter.save();
    painter.translate(position);
    painter.scale(scale, scale);
    painter.setFont(referenceFont(font));
    painter.setPen(color);
    painter.drawText(QPointF(x, y), value);
    painter.restore();
}

QString elided(const Font &font, const QString &value, qreal maxWidth)
{
    if (width(font, value) <= maxWidth)
        return value;
    const QFontMetricsF &m = metrics(font);
    return m.elidedText(value, Qt::ElideRight, maxWidth * referencePx / font.px);
}

} // namespace text

// ---------------------------------------------------------------------------
// Paths

// NOLINTNEXTLINE(readability-function-size): mirrors canvas roundRect, called across the families
QPainterPath roundRect(qreal x, qreal y, qreal w, qreal h, qreal r)
{
    const qreal rr = std::max<qreal>(0, std::min({r, w / 2, h / 2}));
    QPainterPath path;
    path.addRoundedRect(QRectF(x, y, w, h), rr, rr);
    return path;
}

QPainterPath roundRect(const QRectF &rect, qreal r)
{
    return roundRect(rect.x(), rect.y(), rect.width(), rect.height(), r);
}

QPainterPath polygon(const QList<QPointF> &points)
{
    QPainterPath path;
    if (points.isEmpty())
        return path;
    path.moveTo(points.first());
    for (qsizetype i = 1; i < points.size(); ++i)
        path.lineTo(points[i]);
    path.closeSubpath();
    return path;
}

QPainterPath ellipse(QPointF center, qreal rx, qreal ry)
{
    QPainterPath path;
    path.addEllipse(center, rx, ry);
    return path;
}

void shadowFill(QPainter &painter, const QPainterPath &path, const QColor &fill, const Style &style)
{
    // A short shadow lifts the paper without thickening the outlines: blur 4,
    // offset 2, approximated by a few faint, growing copies.
    painter.save();
    painter.setPen(Qt::NoPen);
    QColor shadow = style.palette.shadow;
    const qreal alpha = shadow.alphaF();
    const QPainterPath below = path.translated(0, 2);
    for (int i = 0; i < 3; ++i) {
        shadow.setAlphaF(float(alpha * (0.45 - i * 0.12)));
        QPainterPathStroker stroker;
        stroker.setWidth(1.0 + i * 1.6);
        stroker.setJoinStyle(Qt::RoundJoin);
        painter.fillPath(below + stroker.createStroke(below), shadow);
    }
    painter.restore();
    painter.fillPath(path, fill);
}

namespace {

QPen pen(const QColor &color, qreal width, const QList<qreal> &dash)
{
    QPen result(color, width);
    result.setCapStyle(Qt::RoundCap);
    result.setJoinStyle(Qt::RoundJoin);
    if (!dash.isEmpty() && width > 0) {
        // Qt dashes are in line widths; setLineDash() lengths are in units.
        // Round caps would lengthen each dash by a width: flat caps instead.
        QList<qreal> pattern;
        for (const qreal length : dash)
            pattern.append(std::max<qreal>(0.01, length / width));
        if (pattern.size() % 2)
            pattern += pattern;
        result.setCapStyle(Qt::FlatCap);
        result.setDashPattern(pattern);
    }
    return result;
}

} // namespace

// NOLINTNEXTLINE(readability-function-size): mirrors canvas stroke, called across the families
void stroke(QPainter &painter, const QPainterPath &path, const QColor &color, qreal width, const QList<qreal> &dash)
{
    painter.strokePath(path, pen(color, width, dash));
}

// NOLINTNEXTLINE(readability-function-size): mirrors canvas lineTo + stroke, called across the families
void strokeLine(QPainter &painter, QPointF from, QPointF to, const QColor &color, qreal width, const QList<qreal> &dash)
{
    painter.save();
    painter.setPen(pen(color, width, dash));
    painter.drawLine(from, to);
    painter.restore();
}

// ---------------------------------------------------------------------------
// Colors

bool isHexColor(const QString &value)
{
    static const QRegularExpression hex(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    return hex.match(value.trimmed()).hasMatch();
}

QColor parseColor(const QString &value)
{
    if (!isHexColor(value))
        return {};
    return QColor::fromString(value.trimmed());
}

QString hexColor(const QColor &color)
{
    return color.name(QColor::HexRgb).toLower();
}

QColor readableTextColor(const QColor &background, const QColor &dark, const QColor &light)
{
    if (!background.isValid())
        return dark;
    const qreal back = luminance(background);
    const auto contrast = [back](qreal foreground) {
        return (std::max(back, foreground) + 0.05) / (std::min(back, foreground) + 0.05);
    };
    const qreal darkContrast = contrast(luminance(dark));
    const qreal lightContrast = contrast(luminance(light));
    if (std::max(darkContrast, lightContrast) >= 4.5)
        return darkContrast >= lightContrast ? dark : light;
    // Midtone fills may need a deeper ink than the palette's.
    return contrast(0) >= contrast(1) ? QColor(Qt::black) : QColor(Qt::white);
}

QColor mix(const QColor &from, const QColor &to, qreal amount)
{
    return QColor::fromRgbF(float(from.redF() + (to.redF() - from.redF()) * amount),
                            float(from.greenF() + (to.greenF() - from.greenF()) * amount),
                            float(from.blueF() + (to.blueF() - from.blueF()) * amount));
}

QColor withAlpha(const QColor &color, qreal alpha)
{
    QColor result = color;
    result.setAlphaF(float(std::clamp<qreal>(alpha, 0, 1)));
    return result;
}

const QStringList &swatches()
{
    static const QStringList list{
        QStringLiteral("#bbdefb"), QStringLiteral("#b2dfdb"), QStringLiteral("#c8e6c9"), QStringLiteral("#d1c4e9"),
        QStringLiteral("#ffecb3"), QStringLiteral("#ffccbc"), QStringLiteral("#f8bbd0"), QStringLiteral("#cfd8dc"),
        QStringLiteral("#1565c0"), QStringLiteral("#00695c"), QStringLiteral("#2e7d32"), QStringLiteral("#4527a0"),
        QStringLiteral("#ff8f00"), QStringLiteral("#d84315"), QStringLiteral("#ad1457"), QStringLiteral("#37474f"),
        QStringLiteral("#eeeeee"), QStringLiteral("#ffffff"),
    };
    return list;
}

const QStringList &lineColors()
{
    static const QStringList list{
        QStringLiteral("#1565c0"), QStringLiteral("#00695c"), QStringLiteral("#2e7d32"),
        QStringLiteral("#4527a0"), QStringLiteral("#ff8f00"), QStringLiteral("#d84315"),
        QStringLiteral("#ad1457"), QStringLiteral("#37474f"), QStringLiteral("#9e9e9e"),
    };
    return list;
}

} // namespace diagram
