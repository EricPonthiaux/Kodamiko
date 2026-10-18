// Reading an explainer scene: every rule of sidecar's contract is checked,
// and the first one broken refuses the whole scene with sidecar's message.

#include "SceneReader.h"

#include "Explainer.h"
#include "SceneFormat.h"
#include "viewers/diagram/Family.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cmath>
#include <exception>
#include <limits>
#include <optional>
#include <utility>

namespace diagram::explainer {

namespace {

constexpr int maxElements = 200;
constexpr int maxPoints = 512;

// The scene is refused as a whole, with sidecar's messages.
struct Invalid : std::exception {
    explicit Invalid(QString text)
        : message(std::move(text))
    {
    }

    QString message;
};

[[noreturn]] void fail(const QString &message)
{
    throw Invalid(QStringLiteral("Invalid sciilo-board: ") + message);
}

// What a JavaScript template literal prints for a value.
QString jsText(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Undefined:
        return QStringLiteral("undefined");
    case QJsonValue::Null:
        return QStringLiteral("null");
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double:
        return QString::number(value.toDouble(), 'g', QLocale::FloatingPointShortest);
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Array:
    case QJsonValue::Object:
        return value.isArray() ? QString() : QStringLiteral("[object Object]");
    }
    return {};
}

// JavaScript's Number(): null and "" are 0, what does not read is NaN.
qreal jsNumber(const QJsonValue &value)
{
    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    switch (value.type()) {
    case QJsonValue::Null:
        return 0;
    case QJsonValue::Bool:
        return value.toBool() ? 1 : 0;
    case QJsonValue::Double:
        return value.toDouble();
    case QJsonValue::String: {
        const QString text = value.toString().trimmed();
        if (text.isEmpty())
            return 0;
        bool ok = false;
        const qreal number = text.toDouble(&ok);
        return ok ? number : nan;
    }
    case QJsonValue::Array: {
        const QJsonArray array = value.toArray();
        if (array.isEmpty())
            return 0;
        return array.size() == 1 && !array[0].isArray() && !array[0].isObject() ? jsNumber(array[0]) : nan;
    }
    default:
        return nan;
    }
}

// `value ?? fallback`.
QJsonValue orDefault(const QJsonValue &value, const QJsonValue &fallback)
{
    return value.isUndefined() || value.isNull() ? fallback : value;
}

QJsonObject object(const QJsonValue &value, const QString &name)
{
    if (!value.isObject())
        fail(name + QStringLiteral(" must be an object"));
    return value.toObject();
}

qreal number(const QJsonValue &value, const QString &name, qreal min = -100000, qreal max = 100000)
{
    const qreal parsed = jsNumber(value);
    if (!std::isfinite(parsed) || parsed < min || parsed > max)
        fail(name + QStringLiteral(" is out of range"));
    return parsed;
}

QString text(const QJsonValue &value, const QString &name, int max = 4000)
{
    if (value.isUndefined() || value.isNull())
        return {};
    if (!value.isString() || value.toString().size() > max)
        fail(name + QStringLiteral(" must be a short string"));
    return value.toString();
}

QString optionalEnum(const QJsonValue &value, const QStringList &values, const QString &fallback, const QString &name)
{
    if (value.isUndefined() || value.isNull())
        return fallback;
    if (!value.isString() || !values.contains(value.toString()))
        fail(name + QStringLiteral(" is unsupported"));
    return value.toString();
}

// A tone is presentation metadata, not scene structure. Older agents and
// future clients may use a palette name this version does not know yet; that
// must not make the whole document impossible to open. Unknown tones lose
// only their color and are normalized on the next save.
QString optionalTone(const QJsonValue &value)
{
    return value.isString() && toneNames().contains(value.toString()) ? value.toString() : QStringLiteral("neutral");
}

int optionalAppearAt(const QJsonValue &value)
{
    if (value.isUndefined() || value.isNull())
        return 0;
    const qreal parsed = jsNumber(value);
    return std::isfinite(parsed) && parsed == std::floor(parsed) && parsed >= 1 && parsed <= 1000 ? int(parsed) : 0;
}

QJsonArray strokePoint(const QJsonValue &raw, const QString &strokeId, int index)
{
    const QString name = QStringLiteral("%1.points[%2]").arg(strokeId).arg(index);
    if (raw.isArray() && raw.toArray().size() == 2) {
        const QJsonArray pair = raw.toArray();
        return {number(pair[0], name + QStringLiteral("[0]")), number(pair[1], name + QStringLiteral("[1]"))};
    }
    if (raw.isObject()) {
        const QJsonObject point = raw.toObject();
        const QJsonValue x = point.value(QStringLiteral("x"));
        const QJsonValue y = point.value(QStringLiteral("y"));
        if (!x.isUndefined() && !x.isNull() && !y.isUndefined() && !y.isNull())
            return {number(x, name + QStringLiteral(".x")), number(y, name + QStringLiteral(".y"))};
    }
    fail(name + QStringLiteral(" must be [x, y] or {x, y}"));
}

// The properties every element shares, as the shapes and links read them.
QJsonObject common(const QJsonObject &element, QString *id)
{
    *id = text(element.value(QStringLiteral("id")), QStringLiteral("element.id"), 80);
    if (!isSafeId(*id))
        fail(QStringLiteral("invalid element id \"%1\"").arg(*id));
    QJsonObject props{{QStringLiteral("key"), *id}};
    const QString description =
        text(element.value(QStringLiteral("description")), *id + QStringLiteral(".description"));
    if (!description.isEmpty())
        props.insert(QStringLiteral("description"), description);
    props.insert(QStringLiteral("tone"), optionalTone(element.value(QStringLiteral("tone"))));
    if (const int appearAt = optionalAppearAt(element.value(QStringLiteral("appearAt"))))
        props.insert(QStringLiteral("appearAt"), appearAt);
    const QJsonValue fill = element.value(QStringLiteral("fill"));
    if (!fill.isUndefined() && !fill.isNull()) {
        const QString color = text(fill, *id + QStringLiteral(".fill"), 7).toLower();
        static const QRegularExpression hex(QStringLiteral("^#[\\da-f]{6}$"));
        if (!hex.match(color).hasMatch())
            fail(*id + QStringLiteral(".fill must be a six-digit hex color"));
        props.insert(QStringLiteral("customFill"), color);
    }
    const QJsonValue parent = element.value(QStringLiteral("parent"));
    if (!parent.isUndefined() && !parent.isNull()) {
        const QString frame = text(parent, *id + QStringLiteral(".parent"), 80);
        if (!isSafeId(frame))
            fail(*id + QStringLiteral(".parent must be a safe frame id"));
        props.insert(QStringLiteral("parentBoundary"), frame);
    }
    return props;
}

// A node's or a frame's box. Without any coordinate (an element written by
// hand or by Claude), it is left to the layout; sidecar requires them.
void box(const QJsonObject &element, const QString &id, QJsonObject &props)
{
    if (!element.contains(QStringLiteral("x")) && !element.contains(QStringLiteral("y"))) {
        // No place: the layout gives one; the size stays the declared one.
    } else {
        props.insert(QStringLiteral("x"), number(element.value(QStringLiteral("x")), id + QStringLiteral(".x")));
        props.insert(QStringLiteral("y"), number(element.value(QStringLiteral("y")), id + QStringLiteral(".y")));
    }
    props.insert(QStringLiteral("w"), number(orDefault(element.value(QStringLiteral("width")), 180),
                                             id + QStringLiteral(".width"), 24, 5000));
    props.insert(QStringLiteral("h"), number(orDefault(element.value(QStringLiteral("height")), 76),
                                             id + QStringLiteral(".height"), 20, 5000));
}

// An edge's end: an element, or a free point (a hidden anchor).
QString endpoint(const QJsonValue &value, const QString &edgeId, const QString &side, Model &model)
{
    if (value.isString())
        return value.toString();
    const QJsonObject end = object(value, edgeId + QLatin1Char('.') + side);
    if (end.value(QStringLiteral("element")).isString())
        return end.value(QStringLiteral("element")).toString();
    const QJsonObject point =
        end.value(QStringLiteral("point")).isObject() ? end.value(QStringLiteral("point")).toObject() : end;
    const qreal x = number(point.value(QStringLiteral("x")), edgeId + QLatin1Char('.') + side + QStringLiteral(".x"));
    const qreal y = number(point.value(QStringLiteral("y")), edgeId + QLatin1Char('.') + side + QStringLiteral(".y"));
    const QString key = QStringLiteral("__%1:%2").arg(edgeId, side);
    model.nodes.append({QStringLiteral("freeAnchor"), key,
                        QJsonObject{{QStringLiteral("x"), x - 1},
                                    {QStringLiteral("y"), y - 1},
                                    {QStringLiteral("w"), 2},
                                    {QStringLiteral("h"), 2},
                                    {QStringLiteral("sourceEdgeId"), edgeId},
                                    {QStringLiteral("endpoint"), side}}});
    return key;
}

QJsonObject validateCanvas(const QJsonValue &value)
{
    if (value.isUndefined() || value.isNull())
        return {{QStringLiteral("width"), 1200}, {QStringLiteral("height"), 800}};
    const QJsonObject canvas = object(value, QStringLiteral("canvas"));
    return {{QStringLiteral("width"), number(orDefault(canvas.value(QStringLiteral("width")), 1200),
                                             QStringLiteral("canvas.width"), 200, 10000)},
            {QStringLiteral("height"), number(orDefault(canvas.value(QStringLiteral("height")), 800),
                                              QStringLiteral("canvas.height"), 200, 10000)}};
}

QJsonArray validateSteps(const QJsonValue &value)
{
    if (value.isUndefined() || value.isNull())
        return {};
    if (!value.isArray() || value.toArray().size() > 100)
        fail(QStringLiteral("steps must be an array of at most 100 entries"));
    QJsonArray steps;
    int index = 0;
    for (const QJsonValue &raw : value.toArray()) {
        const QString name = QStringLiteral("steps[%1]").arg(index);
        const QJsonObject step = object(raw, name);
        const QJsonValue id = orDefault(step.value(QStringLiteral("id")), index + 1);
        if (!id.isString() && !id.isDouble())
            fail(name + QStringLiteral(".id is invalid"));
        steps.append(QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("title"), text(step.value(QStringLiteral("title")), name + QStringLiteral(".title"), 240)},
            {QStringLiteral("narration"),
             text(step.value(QStringLiteral("narration")), name + QStringLiteral(".narration"), 4000)}});
        ++index;
    }
    return steps;
}

QJsonObject parseDocument(const QString &source)
{
    QJsonParseError error;
    const QJsonDocument parsed = QJsonDocument::fromJson(source.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
        fail(QStringLiteral("source is not valid JSON"));
    if (!parsed.isObject())
        fail(QStringLiteral("source must be an object"));
    return parsed.object();
}

// The document's elements, once its envelope (format, version, size) holds.
QJsonArray validatedElements(const QJsonObject &document)
{
    if (document.value(QStringLiteral("format")).toString() != sceneFormat)
        fail(QStringLiteral("format must be \"%1\"").arg(sceneFormat));
    const QJsonValue versionValue = document.value(QStringLiteral("version"));
    if (!versionValue.isDouble() || versionValue.toDouble() != sceneVersion)
        fail(QStringLiteral("version must be %1").arg(sceneVersion));
    if (!document.value(QStringLiteral("elements")).isArray())
        fail(QStringLiteral("elements must be an array"));
    const QJsonArray elements = document.value(QStringLiteral("elements")).toArray();
    if (elements.size() > maxElements)
        fail(QStringLiteral("at most %1 elements are allowed").arg(maxElements));
    return elements;
}

void readNode(const QJsonObject &element, const QString &id, QJsonObject props, Model &model)
{
    box(element, id, props);
    props.insert(QStringLiteral("label"),
                 text(element.value(QStringLiteral("label")), id + QStringLiteral(".label"), 1000));
    props.insert(QStringLiteral("shape"), optionalEnum(element.value(QStringLiteral("shape")), shapeNames(),
                                                       QStringLiteral("rounded"), id + QStringLiteral(".shape")));
    model.nodes.append({QStringLiteral("freeNode"), id, props});
}

void readFrame(const QJsonObject &element, const QString &id, QJsonObject props, Model &model)
{
    box(element, id, props);
    props.insert(QStringLiteral("title"),
                 text(orDefault(element.value(QStringLiteral("title")), element.value(QStringLiteral("label"))),
                      id + QStringLiteral(".title"), 500));
    model.nodes.append({QStringLiteral("freeFrame"), id, props});
}

QJsonArray strokePoints(const QJsonObject &element, const QString &strokeId)
{
    const QJsonValue pointsValue = element.value(QStringLiteral("points"));
    const QJsonArray rawPoints = pointsValue.toArray();
    if (!pointsValue.isArray() || rawPoints.size() < 2 || rawPoints.size() > maxPoints)
        fail(QStringLiteral("%1.points must contain 2 to %2 points").arg(strokeId).arg(maxPoints));
    QJsonArray points;
    for (int i = 0; i < rawPoints.size(); ++i)
        points.append(strokePoint(rawPoints[i], strokeId, i));
    return points;
}

void readStroke(const QJsonObject &element, const QString &id, QJsonObject props, Model &model)
{
    const QJsonArray points = strokePoints(element, id);
    props.insert(QStringLiteral("x"),
                 number(orDefault(element.value(QStringLiteral("x")), 0), id + QStringLiteral(".x")));
    props.insert(QStringLiteral("y"),
                 number(orDefault(element.value(QStringLiteral("y")), 0), id + QStringLiteral(".y")));
    props.insert(QStringLiteral("points"), points);
    props.insert(QStringLiteral("label"),
                 text(element.value(QStringLiteral("label")), id + QStringLiteral(".label"), 500));
    props.insert(QStringLiteral("arrow"), optionalEnum(element.value(QStringLiteral("arrow")), arrowNames(),
                                                       QStringLiteral("none"), id + QStringLiteral(".arrow")));
    props.insert(QStringLiteral("line"), optionalEnum(element.value(QStringLiteral("line")), lineNames(),
                                                      QStringLiteral("solid"), id + QStringLiteral(".line")));
    model.nodes.append({QStringLiteral("freeStroke"), id, props});
}

// An edge waits for every element to be read: its ends may come later.
struct PendingEdge {
    QJsonObject element;
    QString id;
    QJsonObject shared; // the properties every element shares
};

struct ShapePass {
    QList<PendingEdge> edges;
    QJsonArray order; // every element id, as written
};

ShapePass readShapes(const QJsonArray &elements, Model &model)
{
    static const QStringList types{QStringLiteral("node"), QStringLiteral("frame"), QStringLiteral("edge"),
                                   QStringLiteral("stroke")};
    ShapePass pass;
    QSet<QString> ids;
    for (const QJsonValue &raw : elements) {
        const QJsonObject element = object(raw, QStringLiteral("element"));
        const QJsonValue type = element.value(QStringLiteral("type"));
        if (!type.isString() || !types.contains(type.toString()))
            fail(QStringLiteral("unsupported element type \"%1\"").arg(jsText(type)));
        QString id;
        const QJsonObject props = common(element, &id);
        if (ids.contains(id))
            fail(QStringLiteral("duplicate element id \"%1\"").arg(id));
        ids.insert(id);
        pass.order.append(id);
        const QString kind = type.toString();
        if (kind == QLatin1String("node"))
            readNode(element, id, props, model);
        else if (kind == QLatin1String("frame"))
            readFrame(element, id, props, model);
        else if (kind == QLatin1String("stroke"))
            readStroke(element, id, props, model);
        else
            pass.edges.append({element, id, props});
    }
    return pass;
}

void checkNoFrameCycle(const QString &key, const QHash<QString, QString> &parents)
{
    QSet<QString> seen{key};
    for (QString up = parents.value(key); !up.isEmpty(); up = parents.value(up)) {
        if (seen.contains(up))
            fail(key + QStringLiteral(".parent creates a frame cycle"));
        seen.insert(up);
    }
}

// Membership: a parent is another frame, and the chain never loops.
void validateMembership(const Model &model)
{
    QSet<QString> frames;
    QHash<QString, QString> parents;
    for (const Model::Node &node : model.nodes) {
        if (node.type == QLatin1String("freeFrame"))
            frames.insert(node.key);
        const QString parent = node.props.value(QStringLiteral("parentBoundary")).toString();
        if (!parent.isEmpty())
            parents.insert(node.key, parent);
    }
    for (const Model::Node &node : model.nodes) {
        const QString parent = parents.value(node.key);
        if (parent.isEmpty())
            continue;
        if (!frames.contains(parent) || parent == node.key)
            fail(node.key + QStringLiteral(".parent must reference another frame"));
        checkNoFrameCycle(node.key, parents);
    }
}

void readEdge(const PendingEdge &edge, const QSet<QString> &visible, Model &model)
{
    const QJsonObject &element = edge.element;
    const QString &id = edge.id;
    const QString from = endpoint(element.value(QStringLiteral("from")), id, QStringLiteral("from"), model);
    const QString to = endpoint(element.value(QStringLiteral("to")), id, QStringLiteral("to"), model);
    for (const QString &end : {from, to}) {
        if (!end.startsWith(QLatin1String("__")) && !visible.contains(end))
            fail(QStringLiteral("%1 references unknown element \"%2\"").arg(id, end));
    }
    QJsonObject props = edge.shared;
    props.remove(QStringLiteral("customFill"));
    props.remove(QStringLiteral("parentBoundary"));
    props.insert(QStringLiteral("label"),
                 text(element.value(QStringLiteral("label")), id + QStringLiteral(".label"), 500));
    props.insert(QStringLiteral("arrow"), optionalEnum(element.value(QStringLiteral("arrow")), arrowNames(),
                                                       QStringLiteral("end"), id + QStringLiteral(".arrow")));
    const QString line = optionalEnum(element.value(QStringLiteral("line")), lineNames(), QStringLiteral("solid"),
                                      id + QStringLiteral(".line"));
    if (line != QLatin1String("solid"))
        props.insert(QStringLiteral("customLineStyle"), line);
    model.links.append({QStringLiteral("freeEdge"), from, to, props});
}

void parseScene(const QString &source, Model &model)
{
    const QJsonObject document = parseDocument(source);
    const ShapePass pass = readShapes(validatedElements(document), model);
    validateMembership(model);
    QSet<QString> visible;
    for (const Model::Node &node : model.nodes)
        visible.insert(node.key);
    for (const PendingEdge &edge : pass.edges)
        readEdge(edge, visible, model);
    model.meta = QJsonObject{{QStringLiteral("canvas"), validateCanvas(document.value(QStringLiteral("canvas")))},
                             {QStringLiteral("steps"), validateSteps(document.value(QStringLiteral("steps")))},
                             {QStringLiteral("order"), pass.order}};
}
} // namespace

std::optional<QString> readScene(const QString &source, Model &model)
{
    try {
        parseScene(source, model);
    } catch (const Invalid &invalid) {
        model = Model();
        return invalid.message;
    }
    return std::nullopt;
}

} // namespace diagram::explainer
