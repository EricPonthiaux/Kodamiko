// SPDX-License-Identifier: GPL-3.0-only

#include "Shape.h"

#include "Board.h"

#include <QJsonArray>
#include <QPainter>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace diagram {

namespace {

qreal clamp(qreal value, qreal low, qreal high)
{
    return std::max(low, std::min(value, high));
}

QJsonArray point(QPointF p)
{
    return QJsonArray{std::round(p.x() * 100) / 100, std::round(p.y() * 100) / 100};
}

std::optional<QPointF> pointFrom(const QJsonValue &value)
{
    const QJsonArray array = value.toArray();
    if (array.size() != 2 || !array[0].isDouble() || !array[1].isDouble())
        return std::nullopt;
    return QPointF(array[0].toDouble(), array[1].toDouble());
}

} // namespace

// ---------------------------------------------------------------------------
// JSON helpers

QString jsonString(const QJsonObject &props, const QString &name, const QString &fallback)
{
    const QJsonValue value = props.value(name);
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return QString::number(value.toDouble());
    return fallback;
}

qreal jsonNumber(const QJsonObject &props, const QString &name, qreal fallback)
{
    const QJsonValue value = props.value(name);
    if (value.isDouble())
        return value.toDouble();
    if (value.isString()) {
        bool ok = false;
        const qreal number = value.toString().toDouble(&ok);
        if (ok && std::isfinite(number))
            return number;
    }
    return fallback;
}

bool jsonBool(const QJsonObject &props, const QString &name, bool fallback)
{
    const QJsonValue value = props.value(name);
    return value.isBool() ? value.toBool() : fallback;
}

QStringList jsonStrings(const QJsonObject &props, const QString &name)
{
    QStringList result;
    for (const QJsonValue &value : props.value(name).toArray()) {
        if (value.isString())
            result.append(value.toString());
        else if (value.isDouble())
            result.append(QString::number(value.toDouble()));
    }
    return result;
}

QJsonArray toJsonArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values)
        array.append(value);
    return array;
}

// ---------------------------------------------------------------------------
// Shape

Shape::Shape(QString typeName, qreal width, qreal height)
    : type(std::move(typeName))
    , w(width)
    , h(height)
{
}

Shape::~Shape() = default;

void Shape::load(const QJsonObject &props)
{
    if (props.value(QStringLiteral("id")).isString())
        id = props.value(QStringLiteral("id")).toString();
    key = jsonString(props, QStringLiteral("key"));
    x = jsonNumber(props, QStringLiteral("x"), x);
    y = jsonNumber(props, QStringLiteral("y"), y);
    w = jsonNumber(props, QStringLiteral("w"), w);
    h = jsonNumber(props, QStringLiteral("h"), h);
    const QString fill = jsonString(props, QStringLiteral("customFill"));
    customFill = isHexColor(fill) ? fill.toLower() : QString();
    parentBoundary = jsonString(props, QStringLiteral("parentBoundary"));
    codeLink = jsonString(props, QStringLiteral("code"));
    const int order = int(jsonNumber(props, QStringLiteral("appearAt"), 0));
    appearAt = order >= 1 && order <= 1000 ? order : 0;
}

QJsonObject Shape::toJson() const
{
    QJsonObject json{
        {QStringLiteral("id"), id}, {QStringLiteral("type"), type}, {QStringLiteral("x"), x},
        {QStringLiteral("y"), y},   {QStringLiteral("w"), w},       {QStringLiteral("h"), h},
    };
    if (!key.isEmpty())
        json.insert(QStringLiteral("key"), key);
    if (!customFill.isEmpty())
        json.insert(QStringLiteral("customFill"), customFill);
    if (!parentBoundary.isEmpty())
        json.insert(QStringLiteral("parentBoundary"), parentBoundary);
    if (!codeLink.isEmpty())
        json.insert(QStringLiteral("code"), codeLink);
    if (appearAt)
        json.insert(QStringLiteral("appearAt"), appearAt);
    return json;
}

void Shape::resizeTo(qreal width, qreal height, const Board &board)
{
    qreal minWidth = std::max<qreal>(1, resizeMinWidth);
    qreal minHeight = std::max<qreal>(1, resizeMinHeight);
    for (const Shape *child : board.childShapes(this)) {
        minWidth = std::max(minWidth, child->x + child->w - x + 12);
        minHeight = std::max(minHeight, child->y + child->h - y + 12);
    }
    w = std::max(width > 0 ? width : 1, minWidth);
    h = std::max(height > 0 ? height : 1, minHeight);
    if (!applyingMeasuredSize) {
        manualWidth = w;
        manualHeight = h;
    }
}

void Shape::measure(const Board &)
{
}

std::optional<QSizeF> Shape::resizeMinimum(const Board &) const
{
    return std::nullopt;
}

QList<Port> Shape::ports(const Board &) const
{
    return {{cx(), y, {}}, {x + w, cy(), {}}, {cx(), y + h, {}}, {x, cy(), {}}};
}

bool Shape::connectionHitTest(const Board &, QPointF point) const
{
    return hitTest(point);
}

QPointF Shape::connectionPoint(const Board &, QPointF from, QPointF) const
{
    return borderPoint(from);
}

std::optional<Port> Shape::connectionPortAt(const Board &, QPointF) const
{
    return std::nullopt;
}

bool Shape::hitTest(QPointF point) const
{
    return point.x() >= x && point.x() <= x + w && point.y() >= y && point.y() <= y + h;
}

QPointF Shape::borderPoint(QPointF toward) const
{
    const qreal dx = toward.x() - cx();
    const qreal dy = toward.y() - cy();
    if (!dx && !dy)
        return {cx(), cy()};
    const qreal inf = std::numeric_limits<qreal>::infinity();
    const qreal sx = dx ? (w / 2) / std::abs(dx) : inf;
    const qreal sy = dy ? (h / 2) / std::abs(dy) : inf;
    const qreal s = std::min(sx, sy);
    return {cx() + dx * s, cy() + dy * s};
}

void Shape::onAdded(Board &)
{
}

void Shape::onDoubleClick(Board &)
{
}

void Shape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QPainterPath path = roundRect(x, y, w, h, style.shape.radius);
    shadowFill(painter, path, fillColor(style.shape.fill), style);
    stroke(painter, path, strokeColor(state.selected ? style.shape.selectedStroke : style.shape.stroke),
           state.selected ? 2 : 1);
}

std::optional<QRectF> Shape::bounds(const Board &) const
{
    return std::nullopt;
}

std::optional<QRectF> Shape::childBounds(const Board &) const
{
    return std::nullopt;
}

QStringList Shape::graphvizNodeAttributes(const Board &, const QString &, qreal, qreal) const
{
    return {};
}

QString Shape::displayText() const
{
    return key;
}

std::optional<CodeReference> Shape::codeReferenceAt(QPointF) const
{
    if (!codeLink.isEmpty())
        return codeReferenceOf(codeLink);
    return codeReferenceIn(displayText().section(QLatin1Char('\n'), 0, 0));
}

std::optional<CodeReference> codeReferenceOf(const QString &link)
{
    const QString written = link.trimmed();
    if (written.isEmpty())
        return std::nullopt;
    const qsizetype hash = written.indexOf(QLatin1Char('#'));
    const QString filePath = hash < 0 ? QString() : written.left(hash);
    const QString symbolText = hash < 0 ? written : written.mid(hash + 1);
    std::optional<CodeReference> symbol = codeReferenceIn(symbolText);
    if (!symbol)
        return CodeReference{.filePath = hash < 0 ? written : filePath};
    symbol->filePath = filePath;
    return symbol;
}

std::optional<CodeReference> codeReferenceIn(const QString &text)
{
    static const QRegularExpression generics(QStringLiteral("~[^~]*~|<.*>"));
    static const QRegularExpression qualifiedName(QStringLiteral("^(?:([A-Za-z_]\\w*)::)*([A-Za-z_]\\w*)$"));
    const QString bare = QString(text).remove(generics).trimmed();
    const QRegularExpressionMatch match = qualifiedName.match(bare);
    if (!match.hasMatch())
        return std::nullopt;
    return CodeReference{.name = match.captured(2), .container = match.captured(1)};
}

QVariant Shape::propertyValue(const QString &) const
{
    return {};
}

void Shape::setPropertyValue(const QString &, const QVariant &)
{
}

bool Shape::addImage(Board &, const QByteArray &)
{
    return false;
}

QColor Shape::fillColor(const QColor &fallback) const
{
    const QColor custom = parseColor(customFill);
    return custom.isValid() ? custom : fallback;
}

QColor Shape::strokeColor(const QColor &fallback) const
{
    const QColor custom = parseColor(customFill);
    if (!custom.isValid())
        return fallback;
    return {qRound(custom.red() * 0.75), qRound(custom.green() * 0.75), qRound(custom.blue() * 0.75)};
}

QColor Shape::textColor(const QColor &fallback) const
{
    const QColor custom = parseColor(customFill);
    // A fill chosen by the user is the same in every theme: the text follows
    // the fill, not the theme.
    return custom.isValid() ? readableTextColor(custom, QColor(0x1a, 0x23, 0x7e)) : fallback;
}

void measureShape(const Board &board, Shape &shape, const QJsonObject *props)
{
    if (props) {
        if (props->value(QStringLiteral("w")).isDouble())
            shape.manualWidth = props->value(QStringLiteral("w")).toDouble();
        if (props->value(QStringLiteral("h")).isDouble())
            shape.manualHeight = props->value(QStringLiteral("h")).toDouble();
    }
    shape.measure(board);
    if (!shape.resizable())
        return;
    const std::optional<QSizeF> declared = shape.resizeMinimum(board);
    shape.resizeMinWidth = std::max<qreal>(1, declared ? declared->width() : shape.w);
    shape.resizeMinHeight = std::max<qreal>(1, declared ? declared->height() : shape.h);
    const qreal desiredWidth = shape.manualWidth.value_or(shape.resizeMinWidth);
    const qreal desiredHeight = shape.manualHeight.value_or(shape.resizeMinHeight);
    shape.applyingMeasuredSize = true;
    shape.resizeTo(desiredWidth, desiredHeight, board);
    shape.applyingMeasuredSize = false;
}

// ---------------------------------------------------------------------------
// Geometry helpers

QPointF bezierPoint(const std::array<QPointF, 4> &controls, qreal t)
{
    const qreal u = 1 - t;
    const qreal a = u * u * u;
    const qreal b = 3 * u * u * t;
    const qreal c = 3 * u * t * t;
    const qreal d = t * t * t;
    return a * controls[0] + b * controls[1] + c * controls[2] + d * controls[3];
}

qreal distanceToSegment(QPointF p, QPointF a, QPointF b)
{
    const qreal vx = b.x() - a.x();
    const qreal vy = b.y() - a.y();
    const qreal wx = p.x() - a.x();
    const qreal wy = p.y() - a.y();
    const qreal len2 = vx * vx + vy * vy;
    const qreal t = len2 ? clamp((wx * vx + wy * vy) / len2, 0, 1) : 0;
    return std::hypot(p.x() - (a.x() + t * vx), p.y() - (a.y() + t * vy));
}

QPointF polylineMidpoint(const std::vector<QPointF> &points)
{
    if (points.empty())
        return {};
    std::vector<qreal> lengths;
    qreal total = 0;
    for (size_t i = 1; i < points.size(); ++i) {
        lengths.push_back(std::hypot(points[i].x() - points[i - 1].x(), points[i].y() - points[i - 1].y()));
        total += lengths.back();
    }
    qreal left = total / 2;
    for (size_t i = 0; i < lengths.size(); ++i) {
        if (left <= lengths[i] && lengths[i] > 0) {
            const qreal ratio = left / lengths[i];
            return points[i] + (points[i + 1] - points[i]) * ratio;
        }
        left -= lengths[i];
    }
    return points.back();
}

std::vector<QPointF> trimPolyline(const std::vector<QPointF> &points, qreal trimStart, qreal trimEnd)
{
    if (points.size() < 2 || (!trimStart && !trimEnd))
        return points;
    std::vector<qreal> lengths;
    qreal total = 0;
    for (size_t i = 1; i < points.size(); ++i) {
        lengths.push_back(std::hypot(points[i].x() - points[i - 1].x(), points[i].y() - points[i - 1].y()));
        total += lengths.back();
    }
    if (!total)
        return points;
    const qreal requested = trimStart + trimEnd;
    const qreal scale = requested > total ? total / requested : 1;

    struct At {
        size_t index{};
        QPointF point;
    };

    const auto pointAt = [&](qreal distance) -> At {
        qreal travelled = 0;
        for (size_t i = 0; i < lengths.size(); ++i) {
            if (travelled + lengths[i] >= distance && lengths[i]) {
                const qreal ratio = (distance - travelled) / lengths[i];
                return {i, points[i] + (points[i + 1] - points[i]) * ratio};
            }
            travelled += lengths[i];
        }
        return {points.size() - 2, points.back()};
    };
    const At start = pointAt(trimStart * scale);
    const At end = pointAt(total - trimEnd * scale);
    std::vector<QPointF> result{start.point};
    for (size_t i = start.index + 1; i <= end.index; ++i)
        result.push_back(points[i]);
    result.push_back(end.point);
    return result;
}

QPointF polygonBorderPoint(QPointF center, QPointF toward, const QList<QPointF> &points)
{
    const qreal dx = toward.x() - center.x();
    const qreal dy = toward.y() - center.y();
    if (!dx && !dy)
        return center;
    bool found = false;
    qreal best = 0;
    for (qsizetype i = 0; i < points.size(); ++i) {
        const QPointF a = points[i];
        const QPointF b = points[(i + 1) % points.size()];
        const qreal ex = b.x() - a.x();
        const qreal ey = b.y() - a.y();
        const qreal denominator = dx * ey - dy * ex;
        if (std::abs(denominator) < 1e-9)
            continue;
        const qreal ax = a.x() - center.x();
        const qreal ay = a.y() - center.y();
        const qreal t = (ax * ey - ay * ex) / denominator;
        const qreal u = (ax * dy - ay * dx) / denominator;
        if (t >= 0 && u >= -1e-9 && u <= 1 + 1e-9 && (!found || t < best)) {
            found = true;
            best = t;
        }
    }
    return found ? QPointF(center.x() + dx * best, center.y() + dy * best) : center;
}

QPointF ellipseBorderPoint(QPointF center, QPointF toward, qreal rx, qreal ry)
{
    const qreal dx = toward.x() - center.x();
    const qreal dy = toward.y() - center.y();
    if ((!dx && !dy) || rx <= 0 || ry <= 0)
        return center;
    const qreal t = 1 / std::hypot(dx / rx, dy / ry);
    return {center.x() + dx * t, center.y() + dy * t};
}

// NOLINTNEXTLINE(readability-function-size): mirrors the canvas arrowhead the families call (9 sites)
void fillArrowHead(QPainter &painter, QPointF tip, qreal angle, qreal size, const QColor &color, qreal spread)
{
    QPainterPath path;
    path.moveTo(tip);
    path.lineTo(tip.x() - size * std::cos(angle - spread), tip.y() - size * std::sin(angle - spread));
    path.lineTo(tip.x() - size * std::cos(angle + spread), tip.y() - size * std::sin(angle + spread));
    path.closeSubpath();
    painter.fillPath(path, color);
}

QList<qreal> linkDash(const Link &link, const QList<qreal> &fallback)
{
    if (link.customLineStyle == QLatin1String("solid"))
        return {};
    if (link.customLineStyle == QLatin1String("dashed"))
        return {8, 5};
    if (link.customLineStyle == QLatin1String("dotted"))
        return {2, 5};
    return fallback;
}

// ---------------------------------------------------------------------------
// Spline

QJsonObject Spline::toJson() const
{
    QJsonArray pts;
    for (const QPointF &p : points)
        pts.append(point(p));
    QJsonObject json{{QStringLiteral("pts"), pts},
                     {QStringLiteral("anchors"), QJsonObject{{QStringLiteral("fx"), fromAnchor.x()},
                                                             {QStringLiteral("fy"), fromAnchor.y()},
                                                             {QStringLiteral("tx"), toAnchor.x()},
                                                             {QStringLiteral("ty"), toAnchor.y()}}}};
    if (start)
        json.insert(QStringLiteral("start"), point(*start));
    if (end)
        json.insert(QStringLiteral("end"), point(*end));
    if (labelPoint)
        json.insert(QStringLiteral("labelPoint"), point(*labelPoint));
    return json;
}

std::optional<Spline> Spline::fromJson(const QJsonValue &value)
{
    const QJsonObject json = value.toObject();
    const QJsonArray pts = json.value(QStringLiteral("pts")).toArray();
    if (pts.size() < 4 || (pts.size() - 1) % 3 != 0)
        return std::nullopt;
    Spline spline;
    for (const QJsonValue &p : pts) {
        const std::optional<QPointF> parsed = pointFrom(p);
        if (!parsed)
            return std::nullopt;
        spline.points.push_back(*parsed);
    }
    spline.start = pointFrom(json.value(QStringLiteral("start")));
    spline.end = pointFrom(json.value(QStringLiteral("end")));
    spline.labelPoint = pointFrom(json.value(QStringLiteral("labelPoint")));
    const QJsonObject anchors = json.value(QStringLiteral("anchors")).toObject();
    spline.fromAnchor =
        QPointF(anchors.value(QStringLiteral("fx")).toDouble(), anchors.value(QStringLiteral("fy")).toDouble());
    spline.toAnchor =
        QPointF(anchors.value(QStringLiteral("tx")).toDouble(), anchors.value(QStringLiteral("ty")).toDouble());
    return spline;
}

// ---------------------------------------------------------------------------
// Link

Link::Link(QString typeName)
    : type(std::move(typeName))
{
}

Link::~Link() = default;

void Link::load(const QJsonObject &props)
{
    if (props.value(QStringLiteral("id")).isString())
        id = props.value(QStringLiteral("id")).toString();
    from = jsonString(props, QStringLiteral("from"), from);
    to = jsonString(props, QStringLiteral("to"), to);
    key = jsonString(props, QStringLiteral("key"));
    label = jsonString(props, QStringLiteral("label"));
    const QString line = jsonString(props, QStringLiteral("customLineStyle"));
    customLineStyle =
        line == QLatin1String("solid") || line == QLatin1String("dashed") || line == QLatin1String("dotted")
            ? line
            : QString();
    const QString color = jsonString(props, QStringLiteral("customColor"));
    customColor = isHexColor(color) ? color.toLower() : QString();
    const QJsonValue width = props.value(QStringLiteral("customWidth"));
    if (width.isDouble() && width.toDouble() >= linkWidthMin && width.toDouble() <= linkWidthMax)
        customWidth = width.toDouble();
    else
        customWidth.reset();
    spline = Spline::fromJson(props.value(QStringLiteral("spline")));
}

QJsonObject Link::toJson() const
{
    QJsonObject json{{QStringLiteral("id"), id},
                     {QStringLiteral("type"), type},
                     {QStringLiteral("from"), from},
                     {QStringLiteral("to"), to},
                     {QStringLiteral("label"), label}};
    if (!key.isEmpty())
        json.insert(QStringLiteral("key"), key);
    if (spline)
        json.insert(QStringLiteral("spline"), spline->toJson());
    if (!customLineStyle.isEmpty())
        json.insert(QStringLiteral("customLineStyle"), customLineStyle);
    if (!customColor.isEmpty())
        json.insert(QStringLiteral("customColor"), customColor);
    if (customWidth)
        json.insert(QStringLiteral("customWidth"), *customWidth);
    return json;
}

const Spline *Link::validSpline(const Shape &a, const Shape &b) const
{
    if (!spline)
        return nullptr;
    const Spline &s = *spline;
    if (std::abs(a.x - s.fromAnchor.x()) > 0.5 || std::abs(a.y - s.fromAnchor.y()) > 0.5 ||
        std::abs(b.x - s.toAnchor.x()) > 0.5 || std::abs(b.y - s.toAnchor.y()) > 0.5) {
        // The curve lies as soon as an end moves: the link falls back to a
        // straight line until the next routing.
        spline.reset();
        flatFor_.clear();
        flat_.clear();
        return nullptr;
    }
    return &s;
}

std::optional<QPointF> Link::validLayoutLabelPoint(const Shape &a, const Shape &b) const
{
    if (!layoutLabelPoint)
        return std::nullopt;
    const ReservedLabel &reserved = *layoutLabelPoint;
    if (std::abs(a.x - reserved.fromAnchor.x()) <= 0.5 && std::abs(a.y - reserved.fromAnchor.y()) <= 0.5 &&
        std::abs(b.x - reserved.toAnchor.x()) <= 0.5 && std::abs(b.y - reserved.toAnchor.y()) <= 0.5)
        return reserved.point;
    layoutLabelPoint.reset();
    return std::nullopt;
}

std::optional<Route> Link::route(const Board &board) const
{
    const Shape *a = board.shape(from);
    const Shape *b = board.shape(to);
    if (!a || !b)
        return std::nullopt;
    const bool hasLabel = !layoutLabel(board).isEmpty();
    if (const Spline *s = validSpline(*a, *b)) {
        Route r;
        r.a = s->start.value_or(s->points.front());
        r.b = s->end.value_or(s->points.back());
        r.curve = s->points;
        if (hasLabel)
            r.labelPoint = s->labelPoint ? s->labelPoint : validLayoutLabelPoint(*a, *b);
        return r;
    }
    QPointF pa = a->borderPoint(QPointF(b->cx(), b->cy()));
    QPointF pb = b->borderPoint(QPointF(a->cx(), a->cy()));
    const qreal spread = parallelSpread(board);
    if (spread) {
        const qreal dx = pb.x() - pa.x();
        const qreal dy = pb.y() - pa.y();
        const qreal len = std::hypot(dx, dy) ? std::hypot(dx, dy) : 1;
        // Perpendicular in the pair's canonical frame (independent of the
        // link's direction), so that every parallel shares the same axis.
        const qreal flip = from < to ? 1 : -1;
        const QPointF offset((dy / len) * spread * flip, (-dx / len) * spread * flip);
        pa += offset;
        pb += offset;
    }
    Route r;
    r.a = pa;
    r.b = pb;
    if (hasLabel)
        r.labelPoint = validLayoutLabelPoint(*a, *b);
    return r;
}

const std::vector<QPointF> &Link::flatCurve(const std::vector<QPointF> &curve) const
{
    if (!flat_.empty() && flatFor_ == curve)
        return flat_;
    flat_.clear();
    flat_.push_back(curve.front());
    for (size_t i = 1; i + 2 < curve.size(); i += 3) {
        const QPointF p0 = flat_.back();
        for (int t = 1; t <= 8; ++t)
            flat_.push_back(bezierPoint({p0, curve[i], curve[i + 1], curve[i + 2]}, t / 8.0));
    }
    flatFor_ = curve;
    return flat_;
}

std::vector<QPointF> Link::routePolyline(const Route &r) const
{
    if (r.points.size() > 1)
        return r.points;
    if (!r.curve.empty())
        return flatCurve(r.curve);
    return {r.a, r.b};
}

qreal Link::distanceToRoute(const Route &r, QPointF p) const
{
    const std::vector<QPointF> points = routePolyline(r);
    qreal nearest = std::numeric_limits<qreal>::infinity();
    for (size_t i = 0; i + 1 < points.size(); ++i)
        nearest = std::min(nearest, distanceToSegment(p, points[i], points[i + 1]));
    return nearest;
}

bool Link::hitTest(const Board &board, QPointF point, qreal tolerance) const
{
    const std::optional<Route> r = route(board);
    return r && distanceToRoute(*r, point) <= tolerance;
}

Link::LabelBox Link::labelBox(const Board &board) const
{
    const auto &o = board.style().linkLabel;
    const Font font(o.fontPx);
    LabelBox box;
    box.lines = label.split(QLatin1Char('\n'));
    box.lineHeight = o.fontPx + 2;
    box.width = text::widest(font, box.lines) + o.padX * 2;
    box.height = box.lines.size() * (o.fontPx + 2) + o.padY * 2;
    return box;
}

std::optional<QPointF> Link::labelShift(const Route &r, const Board &board) const
{
    if (from == to || label.isEmpty())
        return std::nullopt;
    const ParallelGroup group = parallelGroup(board);
    if (group.count < 2)
        return std::nullopt;
    const qreal dx = r.b.x() - r.a.x();
    const qreal dy = r.b.y() - r.a.y();
    const qreal length = std::hypot(dx, dy);
    if (!length)
        return std::nullopt;
    const qreal nx = dy / length;
    const qreal ny = -dx / length;
    const LabelBox box = labelBox(board);
    // The box's thickness seen along the perpendicular: its width on a
    // vertical route, its height on a horizontal one.
    const qreal extent = std::abs(nx) * box.width + std::abs(ny) * box.height;
    const qreal step = extent + labelGutter;
    const qreal flip = from < to ? 1 : -1;
    const qreal shift = (group.index - (group.count - 1) / 2.0) * step * flip;
    return QPointF(nx * shift, ny * shift);
}

std::optional<QPointF> Link::reservedLabelPoint(const Route &r, const Board &board) const
{
    if (!r.labelPoint)
        return std::nullopt;
    // The place Graphviz reserved holds for the curve it computed: kept only
    // while it lies along the line drawn.
    const LabelBox box = labelBox(board);
    const qreal reach = std::max(box.width, box.height) + labelGutter;
    return distanceToRoute(r, *r.labelPoint) <= reach ? r.labelPoint : std::nullopt;
}

QPointF Link::labelPos(const Route &r, const Board &board) const
{
    if (const std::optional<QPointF> reserved = reservedLabelPoint(r, board))
        return *reserved;
    const std::optional<QPointF> shift = labelShift(r, board);
    const auto place = [&](QPointF p) { return shift ? p + *shift : p; };
    if (r.points.size() > 1)
        return place(polylineMidpoint(r.points));
    if (!r.curve.empty())
        return place(polylineMidpoint(flatCurve(r.curve)));
    return place((r.a + r.b) / 2);
}

void Link::strokeBody(QPainter &painter, const Route &r, const BodyOptions &o) const
{
    QPainterPath path;
    if (!r.curve.empty()) {
        std::vector<QPointF> pts = r.curve;
        const qreal startAngle = departureAngle(r);
        const qreal endAngle = arrivalAngle(r);
        const QPointF start(r.a.x() + o.trimStart * std::cos(startAngle), r.a.y() + o.trimStart * std::sin(startAngle));
        const QPointF end(r.b.x() - o.trim * std::cos(endAngle), r.b.y() - o.trim * std::sin(endAngle));
        // Each end moves its neighboring control point along, which keeps the
        // tangent.
        const size_t last = pts.size() - 1;
        pts[1] += start - pts[0];
        pts[0] = start;
        pts[last - 1] += end - pts[last];
        pts[last] = end;
        path.moveTo(pts[0]);
        for (size_t i = 1; i + 2 < pts.size(); i += 3)
            path.cubicTo(pts[i], pts[i + 1], pts[i + 2]);
    } else if (r.points.size() > 1) {
        const std::vector<QPointF> points = trimPolyline(r.points, o.trimStart, o.trim);
        path.moveTo(points.front());
        for (size_t i = 1; i < points.size(); ++i)
            path.lineTo(points[i]);
    } else {
        const qreal angle = std::atan2(r.b.y() - r.a.y(), r.b.x() - r.a.x());
        path.moveTo(r.a.x() + o.trimStart * std::cos(angle), r.a.y() + o.trimStart * std::sin(angle));
        path.lineTo(r.b.x() - o.trim * std::cos(angle), r.b.y() - o.trim * std::sin(angle));
    }
    stroke(painter, path, o.color, o.width, linkDash(*this, o.dash));
}

qreal Link::arrivalAngle(const Route &r) const
{
    QPointF previous = r.a;
    if (!r.curve.empty()) {
        const QPointF last = r.curve.back();
        previous = std::hypot(r.b.x() - last.x(), r.b.y() - last.y()) > 0.5 ? last : r.curve[r.curve.size() - 2];
    } else if (r.points.size() > 1) {
        previous = r.points[r.points.size() - 2];
    }
    return std::atan2(r.b.y() - previous.y(), r.b.x() - previous.x());
}

qreal Link::departureAngle(const Route &r) const
{
    const std::vector<QPointF> &path = !r.curve.empty() ? r.curve : r.points;
    QPointF next = r.b;
    for (const QPointF &p : path) {
        if (std::hypot(p.x() - r.a.x(), p.y() - r.a.y()) > 0.5) {
            next = p;
            break;
        }
    }
    return std::atan2(next.y() - r.a.y(), next.x() - r.a.x());
}

Link::ParallelGroup Link::parallelGroup(const Board &board) const
{
    if (from == to)
        return {0, 1};
    ParallelGroup group{0, 0};
    for (const auto &l : board.links()) {
        if ((l->from == from && l->to == to) || (l->from == to && l->to == from)) {
            if (l.get() == this)
                group.index = group.count;
            ++group.count;
        }
    }
    return group;
}

qreal Link::parallelSpread(const Board &board) const
{
    if (from == to)
        return 0; // self-loops draw their own loop
    const ParallelGroup group = parallelGroup(board);
    return group.count < 2 ? 0 : (group.index - (group.count - 1) / 2.0) * 12;
}

LinkStyle Link::drawStyle(const Board &board, bool selected) const
{
    return drawStyle(board, selected, board.style().link.width);
}

LinkStyle Link::drawStyle(const Board &board, bool selected, qreal nativeWidth) const
{
    const Style &style = board.style();
    const qreal width = customWidth.value_or(nativeWidth);
    // Selection adds a highlight without moving the marker or its shaft.
    const qreal markerScale = width / nativeWidth;
    LinkStyle result;
    const QColor custom = parseColor(customColor);
    const QColor native = selected ? style.shape.selectedStroke : style.link.color;
    result.color = custom.isValid() ? custom : native;
    result.width = width + (selected ? 1 : 0);
    result.arrow = style.link.arrow * markerScale;
    result.markerScale = markerScale;
    return result;
}

void Link::draw(QPainter &painter, const Board &board, bool selected) const
{
    const std::optional<Route> r = route(board);
    if (!r)
        return;
    const LinkStyle style = drawStyle(board, selected);
    strokeBody(painter, *r, {style.color, style.width, {}, arrowBaseInset(style.arrow), 0});
    fillArrowHead(painter, r->b, arrivalAngle(*r), style.arrow, style.color);
}

void Link::drawLabel(QPainter &painter, const Board &board, bool editing) const
{
    if (label.isEmpty() || editing)
        return;
    if (const std::optional<Route> r = route(board))
        drawLabelAt(painter, board, *r);
}

void Link::drawLabelAt(QPainter &painter, const Board &board, const Route &r) const
{
    const auto &o = board.style().linkLabel;
    const QPointF m = labelPos(r, board);
    const LabelBox box = labelBox(board);
    const QPainterPath pill = roundRect(m.x() - box.width / 2, m.y() - box.height / 2, box.width, box.height, 6);
    shadowFill(painter, pill, o.bg, board.style());
    stroke(painter, pill, o.stroke, 1);
    qreal y = m.y() - ((box.lines.size() - 1) * box.lineHeight) / 2;
    for (const QString &line : box.lines) {
        text::draw(painter, Font(o.fontPx), line, QPointF(m.x(), y), o.color, Align::Center, Baseline::Middle);
        y += box.lineHeight;
    }
}

void Link::drawEndpoints(QPainter &painter, const Board &board) const
{
    const std::optional<Route> r = route(board);
    if (!r)
        return;
    const Style &style = board.style();
    for (const QPointF &p : {r->a, r->b}) {
        const QPainterPath dot = ellipse(p, style.port.radius, style.port.radius);
        painter.fillPath(dot, style.port.fill);
        stroke(painter, dot, style.shape.selectedStroke, 1.5);
    }
}

std::optional<QRectF> Link::bounds(const Board &board) const
{
    const std::optional<Route> r = route(board);
    if (!r)
        return std::nullopt;
    std::optional<QRectF> box;
    const auto growPoint = [&](QPointF p) {
        if (!box) {
            box = QRectF(p, QSizeF(0, 0));
            return;
        }
        box->setLeft(std::min(box->left(), p.x()));
        box->setTop(std::min(box->top(), p.y()));
        box->setRight(std::max(box->right(), p.x()));
        box->setBottom(std::max(box->bottom(), p.y()));
    };
    if (r->points.size() > 1) {
        for (const QPointF &p : r->points)
            growPoint(p);
    } else if (!r->curve.empty()) {
        for (const QPointF &p : flatCurve(r->curve))
            growPoint(p);
    }
    if (!label.isEmpty()) {
        const QPointF m = labelPos(*r, board);
        const LabelBox lb = labelBox(board);
        growPoint(QPointF(m.x() - lb.width / 2, m.y() - lb.height / 2));
        growPoint(QPointF(m.x() + lb.width / 2, m.y() + lb.height / 2));
    }
    return box;
}

QStringList Link::graphvizAttributes(const Board &) const
{
    return {};
}

QString Link::layoutLabel(const Board &) const
{
    return label;
}

QString Link::graphvizHtmlLabel(const Board &) const
{
    return {};
}

std::optional<std::pair<GraphvizEndpoint, GraphvizEndpoint>> Link::graphvizEndpoints(const Board &,
                                                                                     const QString &) const
{
    return std::nullopt;
}

QVariant Link::propertyValue(const QString &) const
{
    return {};
}

void Link::setPropertyValue(const QString &, const QVariant &)
{
}

void Link::onDoubleClick(Board &board)
{
    board.editLinkLabel(*this);
}

} // namespace diagram
