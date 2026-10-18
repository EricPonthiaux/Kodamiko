#include "Explainer.h"

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/families/Families.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace diagram::explainer {

namespace {

const Font font(14);
const Font titleFont(14, 600);
constexpr qreal lineHeight = 20;
constexpr qreal pad = 14;
constexpr qreal maxTextWidth = 280;
// The size of the heads a free board draws itself (strokes).
constexpr qreal strokeArrow = 11;
// Graphviz draws its `triangle` across the full width of a node box but only
// the top three quarters of its height, whatever the aspect ratio. Reserving a
// box a third taller makes its polygon exactly the triangle the board draws.
constexpr qreal graphvizTriangleFill = 0.75;

QString tr(const char *text)
{
    return QCoreApplication::translate("ExplainerDiagram", text);
}

QColor valid(const QColor &color, const QColor &fallback)
{
    return color.isValid() ? color : fallback;
}

QColor toneFill(const Board &board, const QString &tone)
{
    const Style &style = board.style();
    const QColor fallback = style.shape.fill;
    if (tone == QLatin1String("accent"))
        return valid(style.tones.group, fallback);
    if (tone == QLatin1String("info"))
        return valid(style.tones.cool, fallback);
    if (tone == QLatin1String("success"))
        return valid(style.tones.data, valid(style.tones.sage, fallback));
    if (tone == QLatin1String("warning"))
        return valid(style.tones.warm, fallback);
    if (tone == QLatin1String("danger"))
        return valid(style.tones.rose, fallback);
    return valid(style.tones.sage, fallback);
}

QColor toneStroke(const Board &board, const QString &tone)
{
    const Style &style = board.style();
    if (tone == QLatin1String("accent"))
        return valid(style.tones.strong, style.link.color);
    return style.link.color;
}

QString oneOf(const QString &value, const QStringList &allowed, const QString &fallback)
{
    return allowed.contains(value) ? value : fallback;
}

QList<std::pair<QString, QString>> toneChoices()
{
    return {{QStringLiteral("neutral"), tr("Neutral")},  {QStringLiteral("accent"), tr("Accent")},
            {QStringLiteral("info"), tr("Information")}, {QStringLiteral("success"), tr("Success")},
            {QStringLiteral("warning"), tr("Warning")},  {QStringLiteral("danger"), tr("Danger")}};
}

QList<std::pair<QString, QString>> arrowChoices()
{
    return {{QStringLiteral("none"), tr("None")},
            {QStringLiteral("start"), tr("At the start")},
            {QStringLiteral("end"), tr("At the end")},
            {QStringLiteral("both"), tr("Both ends")}};
}

Property descriptionProperty()
{
    return {QStringLiteral("description"), tr("Description"), Property::Kind::Lines, {}};
}

// sidecar's withSmallShadow (blur 4, offset 2) for what has no fill to
// shadow: faint copies a little lower.
void smallShadow(const Board &board, const std::function<void(const QColor &, QPointF)> &paint)
{
    QColor shadow = board.style().palette.shadow;
    const qreal alpha = shadow.alphaF();
    for (int i = 0; i < 2; ++i) {
        shadow.setAlphaF(float(alpha * (0.5 - i * 0.2)));
        paint(shadow, QPointF(0, 1.5 + i));
    }
}

// A filled head whose tip is `tip`, pointing away from `previous`.
void arrowHead(QPainter &painter, QPointF tip, QPointF previous, const QColor &color, qreal size = strokeArrow)
{
    fillArrowHead(painter, tip, std::atan2(tip.y() - previous.y(), tip.x() - previous.x()), size, color);
}

} // namespace

const QStringList &shapeNames()
{
    static const QStringList names{QStringLiteral("rectangle"), QStringLiteral("rounded"),  QStringLiteral("circle"),
                                   QStringLiteral("ellipse"),   QStringLiteral("triangle"), QStringLiteral("diamond"),
                                   QStringLiteral("hexagon"),   QStringLiteral("text")};
    return names;
}

const QStringList &toneNames()
{
    static const QStringList names{QStringLiteral("neutral"), QStringLiteral("accent"),  QStringLiteral("info"),
                                   QStringLiteral("success"), QStringLiteral("warning"), QStringLiteral("danger")};
    return names;
}

const QStringList &arrowNames()
{
    static const QStringList names{QStringLiteral("none"), QStringLiteral("start"), QStringLiteral("end"),
                                   QStringLiteral("both")};
    return names;
}

const QStringList &lineNames()
{
    static const QStringList names{QStringLiteral("solid"), QStringLiteral("dashed"), QStringLiteral("dotted")};
    return names;
}

// ---------------------------------------------------------------------------
// FreeNodeShape

FreeNodeShape::FreeNodeShape()
    : Shape(QStringLiteral("freeNode"), 180, 76)
{
}

void FreeNodeShape::load(const QJsonObject &props)
{
    Shape::load(props);
    label = jsonString(props, QStringLiteral("label"), jsonString(props, QStringLiteral("text")));
    description = jsonString(props, QStringLiteral("description"));
    shape = oneOf(jsonString(props, QStringLiteral("shape")), shapeNames(), QStringLiteral("rounded"));
    tone = oneOf(jsonString(props, QStringLiteral("tone")), toneNames(), QStringLiteral("neutral"));
}

QJsonObject FreeNodeShape::toJson() const
{
    QJsonObject json = Shape::toJson();
    json.insert(QStringLiteral("label"), label);
    json.insert(QStringLiteral("description"), description);
    json.insert(QStringLiteral("shape"), shape);
    json.insert(QStringLiteral("tone"), tone);
    return json;
}

QRectF FreeNodeShape::textRect() const
{
    if (shape == QLatin1String("circle")) {
        const qreal width = safeTextWidth_.value_or(std::min(w, h) / std::sqrt(2.0));
        const qreal height = safeTextHeight_.value_or(width);
        return {cx() - width / 2, cy() - height / 2, width, height};
    }
    if (shape == QLatin1String("ellipse")) {
        const qreal width = w / std::sqrt(2.0), height = h / std::sqrt(2.0);
        return {cx() - width / 2, cy() - height / 2, width, height};
    }
    if (shape == QLatin1String("triangle")) {
        const qreal width = w / 2.25, height = h / 2.2;
        return {cx() - width / 2, labelCenterY() - height / 2, width, height};
    }
    if (shape == QLatin1String("diamond"))
        return {cx() - w / 4, cy() - h / 4, w / 2, h / 2};
    if (shape == QLatin1String("hexagon")) {
        const qreal width = w / 1.5, height = h / 1.5;
        return {cx() - width / 2, cy() - height / 2, width, height};
    }
    return {x + pad, y + pad, std::max<qreal>(20, w - pad * 2), std::max(lineHeight, h - pad * 2)};
}

qreal FreeNodeShape::labelCenterY() const
{
    // A triangle's text sits low, where the outline is wide enough.
    return shape == QLatin1String("triangle") ? y + h * 0.68 : cy();
}

QList<QPointF> FreeNodeShape::polygonPoints() const
{
    const qreal r = x + w, b = y + h;
    if (shape == QLatin1String("triangle"))
        return {{cx(), y}, {r, b}, {x, b}};
    if (shape == QLatin1String("diamond"))
        return {{cx(), y}, {r, cy()}, {cx(), b}, {x, cy()}};
    if (shape == QLatin1String("hexagon"))
        return {{x + w * 0.25, y}, {x + w * 0.75, y}, {r, cy()}, {x + w * 0.75, b}, {x + w * 0.25, b}, {x, cy()}};
    return {};
}

QPainterPath FreeNodeShape::outline() const
{
    if (shape == QLatin1String("circle")) {
        const qreal radius = std::min(w, h) / 2;
        return ellipse(QPointF(cx(), cy()), radius, radius);
    }
    if (shape == QLatin1String("ellipse"))
        return ellipse(QPointF(cx(), cy()), w / 2, h / 2);
    const QList<QPointF> points = polygonPoints();
    if (!points.isEmpty())
        return polygon(points);
    // Every box of a diagram carries the same corner, whatever the family: a
    // free board is not the place to invent a second one.
    return roundRect(x, y, w, h, ShapeRadiusSm);
}

QPointF FreeNodeShape::borderPoint(QPointF toward) const
{
    const QPointF center(cx(), cy());
    if (shape == QLatin1String("circle")) {
        const qreal radius = std::min(w, h) / 2;
        return ellipseBorderPoint(center, toward, radius, radius);
    }
    if (shape == QLatin1String("ellipse"))
        return ellipseBorderPoint(center, toward, w / 2, h / 2);
    const QList<QPointF> points = polygonPoints();
    return points.isEmpty() ? Shape::borderPoint(toward) : polygonBorderPoint(center, toward, points);
}

std::optional<QSizeF> FreeNodeShape::graphvizLayoutSize() const
{
    // Graphviz must route against the geometry the board actually draws: left
    // to its default box, a link to a circle or a diamond stops on that
    // invisible rectangle and floats visibly off the outline.
    if (shape != QLatin1String("triangle"))
        return std::nullopt;
    return QSizeF(w, h / graphvizTriangleFill);
}

QStringList FreeNodeShape::graphvizNodeAttributes(const Board &, const QString &, qreal width, qreal height) const
{
    // A shape with no outline of its own (text) routes against its text box.
    static const QHash<QString, QString> geometry{
        {QStringLiteral("rectangle"), QStringLiteral("box")},
        {QStringLiteral("rounded"), QStringLiteral("box")},
        {QStringLiteral("text"), QStringLiteral("box")},
        {QStringLiteral("circle"), QStringLiteral("circle")},
        {QStringLiteral("ellipse"), QStringLiteral("ellipse")},
        {QStringLiteral("triangle"), QStringLiteral("triangle")},
        {QStringLiteral("diamond"), QStringLiteral("diamond")},
        {QStringLiteral("hexagon"), QStringLiteral("hexagon")},
    };
    return {QStringLiteral("shape=") + geometry.value(shape, QStringLiteral("box")),
            QStringLiteral("fixedsize=true"),
            QStringLiteral("margin=0"),
            QStringLiteral("label=\"\""),
            QStringLiteral("width=") + QString::number(std::max<qreal>(1, width) / 72, 'f', 3),
            QStringLiteral("height=") + QString::number(std::max<qreal>(1, height) / 72, 'f', 3)};
}

void FreeNodeShape::measure(const Board &)
{
    // The outline grows with its text and keeps that room (the current size
    // is a floor) until it is explicitly resized; each geometry reserves the
    // area its text box needs inside it.
    const qreal currentWidth = w, currentHeight = h;
    const QStringList probe = text::wrap(font, label, maxTextWidth);
    const qreal safeWidth = std::max<qreal>(28, text::widest(font, probe) + pad * 2);
    const qreal safeHeight = std::max(lineHeight + pad * 2, probe.size() * lineHeight + pad * 2);
    safeTextWidth_ = safeWidth;
    safeTextHeight_ = safeHeight;
    const auto grow = [&](qreal minWidth, qreal minHeight) {
        minWidth_ = std::ceil(minWidth);
        minHeight_ = std::ceil(minHeight);
        w = std::max(currentWidth, minWidth_);
        h = std::max(currentHeight, minHeight_);
    };
    if (shape == QLatin1String("circle")) {
        minWidth_ = std::max<qreal>(96, std::ceil(std::hypot(safeWidth, safeHeight)));
        minHeight_ = minWidth_;
        w = h = std::max({currentWidth, currentHeight, minWidth_});
    } else if (shape == QLatin1String("ellipse")) {
        grow(std::max(140.0, safeWidth * std::sqrt(2.0)), std::max(78.0, safeHeight * std::sqrt(2.0)));
    } else if (shape == QLatin1String("triangle")) {
        grow(std::max(124.0, safeWidth * 2.25), std::max(108.0, safeHeight * 2.2));
    } else if (shape == QLatin1String("diamond")) {
        grow(std::max(132.0, safeWidth * 2), std::max(96.0, safeHeight * 2));
    } else if (shape == QLatin1String("hexagon")) {
        grow(std::max(150.0, safeWidth * 1.5), std::max(78.0, safeHeight * 1.5));
    } else if (shape == QLatin1String("text")) {
        grow(std::max(60.0, safeWidth), std::max(34.0, safeHeight));
    } else {
        grow(std::max(120.0, safeWidth), std::max(56.0, safeHeight));
    }
    lines_ = text::wrap(font, label, std::max<qreal>(20, textRect().width()));
}

void FreeNodeShape::resizeTo(qreal width, qreal height, const Board &)
{
    // Down to the text's floor, never below.
    if (shape == QLatin1String("circle")) {
        w = h = std::max({width, height, minWidth_, minHeight_});
    } else {
        w = std::max(width, minWidth_);
        h = std::max(height, minHeight_);
    }
    lines_ = text::wrap(font, label, std::max<qreal>(20, textRect().width()));
}

void FreeNodeShape::onDoubleClick(Board &board)
{
    TextEditSpec spec;
    spec.get = [this] { return label; };
    spec.set = [this](const QString &value) { label = value; };
    spec.color = board.style().palette.ink;
    spec.font = font;
    spec.lineHeight = lineHeight;
    spec.align = Align::Center;
    spec.rect = [this] { return textRect(); };
    board.beginTextEdit(this, spec);
}

void FreeNodeShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const bool bare = shape == QLatin1String("text");
    const QPainterPath path = outline();
    if (!bare) {
        shadowFill(painter, path, fillColor(toneFill(board, tone)), style);
        stroke(painter, path, strokeColor(state.selected ? style.shape.selectedStroke : style.shape.stroke),
               state.selected ? 2 : ShapeBorderWidth);
    }
    if (state.editing)
        return;
    painter.save();
    if (!bare)
        painter.setClipPath(path, Qt::IntersectClip);
    const QColor ink = textColor(style.palette.ink);
    const QStringList lines = lines_.isEmpty() ? QStringList{QString()} : lines_;
    const auto drawLabel = [&](const QColor &color, QPointF offset) {
        qreal ty = labelCenterY() - ((lines.size() - 1) * lineHeight) / 2;
        for (const QString &line : lines) {
            text::draw(painter, font, line, QPointF(cx(), ty) + offset, color, Align::Center, Baseline::Middle);
            ty += lineHeight;
        }
    };
    // A text without an outline stands out from the paper by a small shadow.
    if (bare)
        smallShadow(board, drawLabel);
    drawLabel(ink, QPointF());
    painter.restore();
}

QList<Property> FreeNodeShape::properties() const
{
    return {
        {QStringLiteral("shape"),
         tr("Shape"),
         Property::Kind::Choice,
         {{QStringLiteral("rounded"), tr("Rounded box")},
          {QStringLiteral("rectangle"), tr("Rectangle")},
          {QStringLiteral("circle"), tr("Circle")},
          {QStringLiteral("ellipse"), tr("Ellipse")},
          {QStringLiteral("triangle"), tr("Triangle")},
          {QStringLiteral("diamond"), tr("Diamond")},
          {QStringLiteral("hexagon"), tr("Hexagon")},
          {QStringLiteral("text"), tr("Text")}}},
        {QStringLiteral("tone"), tr("Tone"), Property::Kind::Choice, toneChoices()},
        descriptionProperty(),
    };
}

QVariant FreeNodeShape::propertyValue(const QString &id) const
{
    if (id == QLatin1String("shape"))
        return shape;
    if (id == QLatin1String("tone"))
        return tone;
    if (id == QLatin1String("description"))
        return description;
    return {};
}

void FreeNodeShape::setPropertyValue(const QString &id, const QVariant &value)
{
    if (id == QLatin1String("shape")) {
        const QString next = oneOf(value.toString(), shapeNames(), shape);
        if (next == shape)
            return;
        shape = next;
        // Another geometry needs another floor: the measure recomputes it
        // from the text instead of keeping the old outline's size.
        manualWidth.reset();
        manualHeight.reset();
        if (shape == QLatin1String("circle"))
            w = h = std::min(w, h);
    } else if (id == QLatin1String("tone")) {
        tone = oneOf(value.toString(), toneNames(), tone);
    } else if (id == QLatin1String("description")) {
        description = value.toString();
    }
}

// ---------------------------------------------------------------------------
// FreeFrameShape

FreeFrameShape::FreeFrameShape()
    : Shape(QStringLiteral("freeFrame"), 360, 240)
{
}

void FreeFrameShape::load(const QJsonObject &props)
{
    Shape::load(props);
    title = jsonString(props, QStringLiteral("title"), jsonString(props, QStringLiteral("label")));
    description = jsonString(props, QStringLiteral("description"));
    tone = oneOf(jsonString(props, QStringLiteral("tone")), toneNames(), QStringLiteral("neutral"));
}

QJsonObject FreeFrameShape::toJson() const
{
    QJsonObject json = Shape::toJson();
    json.insert(QStringLiteral("title"), title);
    json.insert(QStringLiteral("description"), description);
    json.insert(QStringLiteral("tone"), tone);
    return json;
}

qreal FreeFrameShape::layoutLabelFontPx() const
{
    return font.px;
}

std::optional<QSizeF> FreeFrameShape::layoutMinimumSize() const
{
    return QSizeF(minWidth_, minHeight_);
}

void FreeFrameShape::wrapTitle()
{
    titleLines_ = text::wrap(titleFont, title, std::max<qreal>(20, w - pad * 2));
    titleHeight_ = std::max<qreal>(38, titleLines_.size() * lineHeight + 16);
}

void FreeFrameShape::measure(const Board &)
{
    const QStringList probe = text::wrap(titleFont, title, 480);
    minWidth_ = std::ceil(std::max<qreal>(220, text::widest(titleFont, probe) + pad * 2));
    minHeight_ = 140;
    w = std::ceil(std::max(w, minWidth_));
    h = std::ceil(std::max(h, minHeight_));
    wrapTitle();
    minHeight_ = std::max(minHeight_, titleHeight_ + 40);
    h = std::ceil(std::max(h, minHeight_));
}

std::optional<QRectF> FreeFrameShape::childBounds(const Board &) const
{
    return QRectF(QPointF(x + 12, y + titleHeight_ + 8), QPointF(x + w - 12, y + h - 12));
}

void FreeFrameShape::resizeTo(qreal width, qreal height, const Board &board)
{
    // Never smaller than its title, nor than the children it holds.
    const QList<Shape *> children = board.childShapes(this);
    qreal minWidth = minWidth_;
    for (const Shape *child : children)
        minWidth = std::max(minWidth, child->x + child->w - x + 12);
    w = std::max(width, minWidth);
    wrapTitle();
    minHeight_ = std::max<qreal>(140, titleHeight_ + 40);
    qreal minHeight = minHeight_;
    for (const Shape *child : children)
        minHeight = std::max(minHeight, child->y + child->h - y + 12);
    h = std::max(height, minHeight);
}

bool FreeFrameShape::hitTest(QPointF point) const
{
    // A frame is selected from its border and title, leaving its interior
    // free for panning and for the elements it visually groups.
    if (!Shape::hitTest(point))
        return false;
    constexpr qreal inset = 12;
    return point.y() <= y + titleHeight_ || point.x() <= x + inset || point.x() >= x + w - inset ||
           point.y() >= y + h - inset;
}

void FreeFrameShape::onDoubleClick(Board &board)
{
    TextEditSpec spec;
    spec.get = [this] { return title; };
    spec.set = [this](const QString &value) { title = value; };
    spec.color = board.style().palette.ink;
    spec.font = titleFont;
    spec.lineHeight = lineHeight;
    spec.align = Align::Left;
    spec.rect = [this] { return QRectF(x + pad, y + 8, w - pad * 2, std::max<qreal>(24, titleHeight_ - 12)); };
    board.beginTextEdit(this, spec);
}

void FreeFrameShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    // A frame about to take the drag turns solid: the dashed outline says
    // "this is a grouping", the solid one says "let go and it lands here".
    const bool dropTarget = state.dropTarget;
    const QPainterPath path = roundRect(x, y, w, h, ShapeRadiusSm);
    painter.save();
    painter.setOpacity(painter.opacity() * (dropTarget ? 0.5 : 0.3));
    shadowFill(painter, path, fillColor(toneFill(board, tone)), style);
    painter.restore();
    stroke(painter, path, strokeColor(state.selected || dropTarget ? style.shape.selectedStroke : style.shape.stroke),
           dropTarget       ? 2.5
           : state.selected ? 2
                            : 1.2,
           dropTarget ? QList<qreal>{} : QList<qreal>{7, 5});
    if (state.editing || title.isEmpty())
        return;
    const QColor ink = textColor(style.palette.ink);
    const QStringList lines = titleLines_.isEmpty() ? QStringList{title} : titleLines_;
    qreal ty = y + 18;
    for (const QString &line : lines) {
        text::draw(painter, titleFont, line, QPointF(x + pad, ty), ink, Align::Left, Baseline::Middle);
        ty += lineHeight;
    }
}

QList<Property> FreeFrameShape::properties() const
{
    return {{QStringLiteral("tone"), tr("Tone"), Property::Kind::Choice, toneChoices()}, descriptionProperty()};
}

QVariant FreeFrameShape::propertyValue(const QString &id) const
{
    if (id == QLatin1String("tone"))
        return tone;
    if (id == QLatin1String("description"))
        return description;
    return {};
}

void FreeFrameShape::setPropertyValue(const QString &id, const QVariant &value)
{
    if (id == QLatin1String("tone"))
        tone = oneOf(value.toString(), toneNames(), tone);
    else if (id == QLatin1String("description"))
        description = value.toString();
}

// ---------------------------------------------------------------------------
// FreeAnchorShape

FreeAnchorShape::FreeAnchorShape()
    : Shape(QStringLiteral("freeAnchor"), 2, 2)
{
}

void FreeAnchorShape::load(const QJsonObject &props)
{
    Shape::load(props);
    sourceEdgeId = jsonString(props, QStringLiteral("sourceEdgeId"));
    endpoint = jsonString(props, QStringLiteral("endpoint"));
}

QJsonObject FreeAnchorShape::toJson() const
{
    QJsonObject json = Shape::toJson();
    json.insert(QStringLiteral("sourceEdgeId"), sourceEdgeId);
    json.insert(QStringLiteral("endpoint"), endpoint);
    return json;
}

// ---------------------------------------------------------------------------
// FreeStrokeShape

FreeStrokeShape::FreeStrokeShape()
    : Shape(QStringLiteral("freeStroke"), 120, 1)
{
    syncBounds();
}

void FreeStrokeShape::load(const QJsonObject &props)
{
    Shape::load(props);
    const QJsonArray array = props.value(QStringLiteral("points")).toArray();
    if (!array.isEmpty()) {
        points.clear();
        for (const QJsonValue &value : array) {
            const QJsonArray pair = value.toArray();
            points.emplace_back(pair.at(0).toDouble(), pair.at(1).toDouble());
        }
    }
    label = jsonString(props, QStringLiteral("label"));
    description = jsonString(props, QStringLiteral("description"));
    tone = oneOf(jsonString(props, QStringLiteral("tone")), toneNames(), QStringLiteral("neutral"));
    arrow = oneOf(jsonString(props, QStringLiteral("arrow")), arrowNames(), QStringLiteral("none"));
    line = oneOf(jsonString(props, QStringLiteral("line")), lineNames(), QStringLiteral("solid"));
    syncBounds();
}

QJsonObject FreeStrokeShape::toJson() const
{
    QJsonObject json = Shape::toJson();
    QJsonArray array;
    for (const QPointF &p : points)
        array.append(QJsonArray{p.x(), p.y()});
    json.insert(QStringLiteral("points"), array);
    json.insert(QStringLiteral("label"), label);
    json.insert(QStringLiteral("description"), description);
    json.insert(QStringLiteral("tone"), tone);
    json.insert(QStringLiteral("arrow"), arrow);
    json.insert(QStringLiteral("line"), line);
    return json;
}

void FreeStrokeShape::syncBounds()
{
    if (points.empty())
        points = {{0, 0}, {120, 0}};
    qreal minX = points.front().x(), maxX = minX, minY = points.front().y(), maxY = minY;
    for (const QPointF &p : points) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    localBounds_ = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
    w = std::max<qreal>(1, maxX - minX);
    h = std::max<qreal>(1, maxY - minY);
}

void FreeStrokeShape::measure(const Board &)
{
    labelWidth_ = text::width(font, label);
}

QPointF FreeStrokeShape::labelPoint() const
{
    const QPointF p = points.empty() ? QPointF() : points[(points.size() - 1) / 2];
    return {x + p.x(), y + p.y() - 7};
}

QRectF FreeStrokeShape::strokeBounds() const
{
    QRectF result = localBounds_.translated(x, y);
    if (!label.isEmpty()) {
        const QPointF at = labelPoint();
        result = result.united(QRectF(QPointF(at.x() - labelWidth_ / 2 - 4, at.y() - lineHeight),
                                      QPointF(at.x() + labelWidth_ / 2 + 4, at.y() + 4)));
    }
    return result;
}

std::optional<QRectF> FreeStrokeShape::bounds(const Board &) const
{
    return strokeBounds();
}

bool FreeStrokeShape::hitTest(QPointF point) const
{
    for (size_t i = 1; i < points.size(); ++i) {
        if (distanceToSegment(point, points[i - 1] + QPointF(x, y), points[i] + QPointF(x, y)) <= 7)
            return true;
    }
    if (label.isEmpty())
        return false;
    const QPointF at = labelPoint();
    return point.x() >= at.x() - labelWidth_ / 2 - 4 && point.x() <= at.x() + labelWidth_ / 2 + 4 &&
           point.y() >= at.y() - lineHeight && point.y() <= at.y() + 4;
}

void FreeStrokeShape::onDoubleClick(Board &board)
{
    TextEditSpec spec;
    spec.get = [this] { return label; };
    spec.set = [this](const QString &value) { label = value; };
    spec.color = board.style().palette.ink;
    spec.font = font;
    spec.lineHeight = lineHeight;
    spec.align = Align::Center;
    spec.rect = [this] {
        const QPointF at = labelPoint();
        const qreal width = std::max<qreal>(80, labelWidth_ + 20);
        return QRectF(at.x() - width / 2, at.y() - lineHeight, width, lineHeight + 6);
    };
    board.beginTextEdit(this, spec);
}

std::vector<QPointF> FreeStrokeShape::shaft() const
{
    std::vector<QPointF> world;
    for (const QPointF &p : points)
        world.push_back(p + QPointF(x, y));
    // Very short arrows keep their direction and collapse only the shaft:
    // the line never crosses either tip.
    const qreal inset = arrowBaseInset(strokeArrow);
    const bool start = arrow == QLatin1String("start") || arrow == QLatin1String("both");
    const bool end = arrow == QLatin1String("end") || arrow == QLatin1String("both");
    return trimPolyline(world, start ? inset : 0, end ? inset : 0);
}

void FreeStrokeShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    if (points.size() < 2)
        return;
    const QColor color = state.selected ? board.style().shape.selectedStroke : toneStroke(board, tone);
    const qreal width = state.selected ? 3 : 2;
    const QList<qreal> dash = line == QLatin1String("dashed")   ? QList<qreal>{8, 5}
                              : line == QLatin1String("dotted") ? QList<qreal>{2, 5}
                                                                : QList<qreal>{};
    const std::vector<QPointF> body = shaft();
    QPainterPath path;
    path.moveTo(body.front());
    for (size_t i = 1; i < body.size(); ++i)
        path.lineTo(body[i]);
    const QPointF first = points[0] + QPointF(x, y), second = points[1] + QPointF(x, y);
    const QPointF last = points.back() + QPointF(x, y), beforeLast = points[points.size() - 2] + QPointF(x, y);
    const bool start = arrow == QLatin1String("start") || arrow == QLatin1String("both");
    const bool end = arrow == QLatin1String("end") || arrow == QLatin1String("both");
    const auto paint = [&](const QColor &ink, QPointF offset) {
        stroke(painter, path.translated(offset), ink, width, dash);
        if (start)
            arrowHead(painter, first + offset, second + offset, ink);
        if (end)
            arrowHead(painter, last + offset, beforeLast + offset, ink);
    };
    smallShadow(board, paint);
    paint(color, QPointF());
    if (label.isEmpty() || state.editing)
        return;
    text::draw(painter, font, label, labelPoint(), color, Align::Center, Baseline::Bottom);
}

QList<Property> FreeStrokeShape::properties() const
{
    return {
        {QStringLiteral("arrow"), tr("Arrow"), Property::Kind::Choice, arrowChoices()},
        {QStringLiteral("line"),
         tr("Line"),
         Property::Kind::Choice,
         {{QStringLiteral("solid"), tr("Solid")},
          {QStringLiteral("dashed"), tr("Dashed")},
          {QStringLiteral("dotted"), tr("Dotted")}}},
        {QStringLiteral("tone"), tr("Tone"), Property::Kind::Choice, toneChoices()},
        descriptionProperty(),
    };
}

QVariant FreeStrokeShape::propertyValue(const QString &id) const
{
    if (id == QLatin1String("arrow"))
        return arrow;
    if (id == QLatin1String("line"))
        return line;
    if (id == QLatin1String("tone"))
        return tone;
    if (id == QLatin1String("description"))
        return description;
    return {};
}

void FreeStrokeShape::setPropertyValue(const QString &id, const QVariant &value)
{
    if (id == QLatin1String("arrow"))
        arrow = oneOf(value.toString(), arrowNames(), arrow);
    else if (id == QLatin1String("line"))
        line = oneOf(value.toString(), lineNames(), line);
    else if (id == QLatin1String("tone"))
        tone = oneOf(value.toString(), toneNames(), tone);
    else if (id == QLatin1String("description"))
        description = value.toString();
}

// ---------------------------------------------------------------------------
// FreeEdge

FreeEdge::FreeEdge()
    : Link(QStringLiteral("freeEdge"))
{
}

void FreeEdge::load(const QJsonObject &props)
{
    Link::load(props);
    description = jsonString(props, QStringLiteral("description"));
    tone = oneOf(jsonString(props, QStringLiteral("tone")), toneNames(), QStringLiteral("neutral"));
    arrow = oneOf(jsonString(props, QStringLiteral("arrow")), arrowNames(), QStringLiteral("end"));
    const int order = int(jsonNumber(props, QStringLiteral("appearAt"), 0));
    appearAt = order >= 1 && order <= 1000 ? order : 0;
}

QJsonObject FreeEdge::toJson() const
{
    QJsonObject json = Link::toJson();
    json.insert(QStringLiteral("description"), description);
    json.insert(QStringLiteral("tone"), tone);
    json.insert(QStringLiteral("arrow"), arrow);
    if (appearAt)
        json.insert(QStringLiteral("appearAt"), appearAt);
    return json;
}

QStringList FreeEdge::graphvizAttributes(const Board &) const
{
    // A free board draws its own heads, at its own size, on both ends.
    // Graphviz must therefore not shorten the spline for a head of its own:
    // what it reserves is a hole nothing fills, plainly visible on a line
    // without an arrow, which then stops short of the shape it points at.
    return {QStringLiteral("dir=none")};
}

void FreeEdge::draw(QPainter &painter, const Board &board, bool selected) const
{
    const std::optional<Route> r = route(board);
    if (!r)
        return;
    LinkStyle style = Link::drawStyle(board, selected);
    // The tone colors the line unless the user chose a color or it is
    // selected.
    if (customColor.isEmpty() && !selected)
        style.color = toneStroke(board, tone);
    const bool start = arrow == QLatin1String("start") || arrow == QLatin1String("both");
    const bool end = arrow == QLatin1String("end") || arrow == QLatin1String("both");
    const qreal inset = arrowBaseInset(style.arrow);
    strokeBody(painter, *r, {style.color, style.width, {}, end ? inset : 0, start ? inset : 0});
    if (start)
        fillArrowHead(painter, r->a, departureAngle(*r) + M_PI, style.arrow, style.color);
    if (end)
        fillArrowHead(painter, r->b, arrivalAngle(*r), style.arrow, style.color);
}

QList<Property> FreeEdge::properties() const
{
    return {
        {QStringLiteral("arrow"), tr("Arrow"), Property::Kind::Choice, arrowChoices()},
        {QStringLiteral("tone"), tr("Tone"), Property::Kind::Choice, toneChoices()},
        descriptionProperty(),
    };
}

QVariant FreeEdge::propertyValue(const QString &id) const
{
    if (id == QLatin1String("arrow"))
        return arrow;
    if (id == QLatin1String("tone"))
        return tone;
    if (id == QLatin1String("description"))
        return description;
    return {};
}

void FreeEdge::setPropertyValue(const QString &id, const QVariant &value)
{
    if (id == QLatin1String("arrow"))
        arrow = oneOf(value.toString(), arrowNames(), arrow);
    else if (id == QLatin1String("tone"))
        tone = oneOf(value.toString(), toneNames(), tone);
    else if (id == QLatin1String("description"))
        description = value.toString();
}

} // namespace diagram::explainer

// ---------------------------------------------------------------------------
// The family

namespace diagram::families {

const Family &explainer()
{
    using namespace explainer;
    static const Family family = [] {
        Family f;
        f.id = QStringLiteral("explainer");
        f.title = tr("Explainer board");
        f.shapes = {
            {QStringLiteral("freeNode"), [] { return std::unique_ptr<Shape>(new FreeNodeShape); }},
            {QStringLiteral("freeFrame"), [] { return std::unique_ptr<Shape>(new FreeFrameShape); }},
            {QStringLiteral("freeStroke"), [] { return std::unique_ptr<Shape>(new FreeStrokeShape); }},
            {QStringLiteral("freeAnchor"), [] { return std::unique_ptr<Shape>(new FreeAnchorShape); }},
        };
        LinkType edge{QStringLiteral("freeEdge"), [] { return std::unique_ptr<Link>(new FreeEdge); }};
        edge.allowMulti = true;
        f.links = {edge};
        f.defaultShape = QStringLiteral("freeNode");
        f.defaultLink = QStringLiteral("freeEdge");
        f.alignmentGuides = true;
        // Nothing but the pointer decides which frame owns an element here:
        // the source carries no layout intent the engine could defer to.
        f.reparentOnDrop = true;
        f.nestedCreation = true;
        f.graphvizClusters = true;
        // Most elements of an explainer carry no relation at all: without
        // this, Graphviz would rank them all together and spread the board
        // sideways.
        f.graphvizStackDisconnected = true;
        // Moving a shape invalidates the routes drawn around it; recomputing
        // them alone keeps a hand-arranged board exactly as its author left
        // it.
        f.routeLinksOnMove = true;
        f.inferClusterMembership = true;

        const auto node = [](const char *id, const QString &label, qreal w, qreal h, const QString &text,
                             const char *shape, const char *tone) {
            QJsonObject props{{QStringLiteral("w"), w},
                              {QStringLiteral("h"), h},
                              {QStringLiteral("label"), text},
                              {QStringLiteral("shape"), QString::fromLatin1(shape)}};
            if (tone)
                props.insert(QStringLiteral("tone"), QString::fromLatin1(tone));
            return ElementPreset{QString::fromLatin1(id), label, {}, QStringLiteral("freeNode"), props};
        };
        f.elements = {
            node("free-rounded", tr("Rounded box"), 180, 76, tr("New idea"), "rounded", "info"),
            node("free-rectangle", tr("Rectangle"), 180, 76, tr("New element"), "rectangle", nullptr),
            node("free-circle", tr("Circle"), 96, 96, tr("Concept"), "circle", "accent"),
            node("free-ellipse", tr("Ellipse"), 180, 88, tr("Concept"), "ellipse", "success"),
            node("free-triangle", tr("Triangle"), 124, 108, tr("Key point"), "triangle", "warning"),
            node("free-diamond", tr("Diamond"), 132, 96, tr("Question?"), "diamond", "warning"),
            node("free-hexagon", tr("Hexagon"), 180, 82, tr("Step"), "hexagon", "info"),
            node("free-text", tr("Text"), 180, 60, tr("Free text"), "text", nullptr),
            ElementPreset{QStringLiteral("free-frame"),
                          tr("Frame"),
                          {},
                          QStringLiteral("freeFrame"),
                          QJsonObject{{QStringLiteral("title"), tr("New section")},
                                      {QStringLiteral("tone"), QStringLiteral("neutral")}}},
            // Drawn by a press-drag-release: no arbitrary endpoints imposed.
            ElementPreset{QStringLiteral("free-segment"), tr("Draw a line"), tr("Then press and drag on the canvas"),
                          QStringLiteral("freeStroke"), QJsonObject{{QStringLiteral("arrow"), QStringLiteral("none")}},
                          true},
            ElementPreset{QStringLiteral("free-stroke"), tr("Draw an arrow"), tr("Then press and drag on the canvas"),
                          QStringLiteral("freeStroke"),
                          QJsonObject{{QStringLiteral("arrow"), QStringLiteral("end")},
                                      {QStringLiteral("tone"), QStringLiteral("accent")}},
                          true},
        };
        f.relations = {
            {QStringLiteral("free-arrow"),
             tr("Arrow"),
             {},
             QJsonObject{{QStringLiteral("arrow"), QStringLiteral("end")}}},
            {QStringLiteral("free-line"),
             tr("Line"),
             {},
             QJsonObject{{QStringLiteral("arrow"), QStringLiteral("none")}}},
        };
        return f;
    }();
    return family;
}

} // namespace diagram::families
