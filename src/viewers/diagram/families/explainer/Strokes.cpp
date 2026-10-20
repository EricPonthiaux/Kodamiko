// SPDX-License-Identifier: GPL-3.0-only

// The explainer's free strokes (lines and arrows drawn by hand) and its
// edges between elements.

#include "Explainer.h"
#include "Presentation.h"
#include "viewers/diagram/Board.h"

#include <QJsonArray>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace diagram::explainer {

using namespace detail;

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
    qreal minX = points.front().x();
    qreal maxX = minX;
    qreal minY = points.front().y();
    qreal maxY = minY;
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
    world.reserve(points.size());
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
    const QList<qreal> dash = dashOf(line);
    const std::vector<QPointF> body = shaft();
    QPainterPath path;
    path.moveTo(body.front());
    for (size_t i = 1; i < body.size(); ++i)
        path.lineTo(body[i]);
    const QPointF first = points[0] + QPointF(x, y);
    const QPointF second = points[1] + QPointF(x, y);
    const QPointF last = points.back() + QPointF(x, y);
    const QPointF beforeLast = points[points.size() - 2] + QPointF(x, y);
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

QVariant FreeStrokeShape::propertyValue(const QString &propertyId) const
{
    if (propertyId == QLatin1String("arrow"))
        return arrow;
    if (propertyId == QLatin1String("line"))
        return line;
    if (propertyId == QLatin1String("tone"))
        return tone;
    if (propertyId == QLatin1String("description"))
        return description;
    return {};
}

void FreeStrokeShape::setPropertyValue(const QString &propertyId, const QVariant &value)
{
    if (propertyId == QLatin1String("arrow"))
        arrow = oneOf(value.toString(), arrowNames(), arrow);
    else if (propertyId == QLatin1String("line"))
        line = oneOf(value.toString(), lineNames(), line);
    else if (propertyId == QLatin1String("tone"))
        tone = oneOf(value.toString(), toneNames(), tone);
    else if (propertyId == QLatin1String("description"))
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

QVariant FreeEdge::propertyValue(const QString &propertyId) const
{
    if (propertyId == QLatin1String("arrow"))
        return arrow;
    if (propertyId == QLatin1String("tone"))
        return tone;
    if (propertyId == QLatin1String("description"))
        return description;
    return {};
}

void FreeEdge::setPropertyValue(const QString &propertyId, const QVariant &value)
{
    if (propertyId == QLatin1String("arrow"))
        arrow = oneOf(value.toString(), arrowNames(), arrow);
    else if (propertyId == QLatin1String("tone"))
        tone = oneOf(value.toString(), toneNames(), tone);
    else if (propertyId == QLatin1String("description"))
        description = value.toString();
}

} // namespace diagram::explainer
