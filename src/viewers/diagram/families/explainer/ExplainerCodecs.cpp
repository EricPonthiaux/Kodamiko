// Explainer boards as a JSON scene (sidecar's fence/sciilo-board.js and its
// contract, sciilo-board-v1.schema.json): a compact, versioned scene, easy
// for an LLM to produce and safe to render. Unlike a Mermaid diagram, its
// coordinates are source data: an element moved by an agent must not snap
// back to an older layout. The format is sidecar's, so that boards move
// between the two apps.

#include "Explainer.h"
#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/families/Families.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <limits>

namespace diagram::explainer {

namespace {

const QString format = QStringLiteral("sciilo-board");
constexpr int version = 1;
constexpr int maxElements = 200;
constexpr int maxPoints = 512;

const QRegularExpression &idPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:-]{0,79}$"));
    return pattern;
}

bool safeId(const QString &id)
{
    return idPattern().match(id).hasMatch() && !id.startsWith(QLatin1String("__"));
}

// The scene is refused as a whole, with sidecar's messages.
struct Invalid {
    QString message;
};

[[noreturn]] void fail(const QString &message)
{
    throw Invalid{QStringLiteral("Invalid sciilo-board: ") + message};
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
        const QJsonValue x = point.value(QStringLiteral("x")), y = point.value(QStringLiteral("y"));
        if (!x.isUndefined() && !x.isNull() && !y.isUndefined() && !y.isNull())
            return {number(x, name + QStringLiteral(".x")), number(y, name + QStringLiteral(".y"))};
    }
    fail(name + QStringLiteral(" must be [x, y] or {x, y}"));
}

// The properties every element shares, as the shapes and links read them.
QJsonObject common(const QJsonObject &element, QString *id)
{
    *id = text(element.value(QStringLiteral("id")), QStringLiteral("element.id"), 80);
    if (!safeId(*id))
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
        if (!safeId(frame))
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

void parseScene(const QString &source, Model &model)
{
    QJsonParseError error;
    const QJsonDocument parsed = QJsonDocument::fromJson(source.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
        fail(QStringLiteral("source is not valid JSON"));
    if (!parsed.isObject())
        fail(QStringLiteral("source must be an object"));
    const QJsonObject document = parsed.object();
    if (document.value(QStringLiteral("format")).toString() != format)
        fail(QStringLiteral("format must be \"%1\"").arg(format));
    const QJsonValue versionValue = document.value(QStringLiteral("version"));
    if (!versionValue.isDouble() || versionValue.toDouble() != version)
        fail(QStringLiteral("version must be %1").arg(version));
    if (!document.value(QStringLiteral("elements")).isArray())
        fail(QStringLiteral("elements must be an array"));
    const QJsonArray elements = document.value(QStringLiteral("elements")).toArray();
    if (elements.size() > maxElements)
        fail(QStringLiteral("at most %1 elements are allowed").arg(maxElements));

    static const QStringList types{QStringLiteral("node"), QStringLiteral("frame"), QStringLiteral("edge"),
                                   QStringLiteral("stroke")};
    QSet<QString> ids;
    QJsonArray order;
    QList<std::pair<QJsonObject, std::pair<QString, QJsonObject>>> pendingEdges;
    for (const QJsonValue &raw : elements) {
        const QJsonObject element = object(raw, QStringLiteral("element"));
        const QJsonValue type = element.value(QStringLiteral("type"));
        if (!type.isString() || !types.contains(type.toString()))
            fail(QStringLiteral("unsupported element type \"%1\"").arg(jsText(type)));
        QString id;
        QJsonObject props = common(element, &id);
        if (ids.contains(id))
            fail(QStringLiteral("duplicate element id \"%1\"").arg(id));
        ids.insert(id);
        order.append(id);

        if (type.toString() == QLatin1String("node")) {
            box(element, id, props);
            props.insert(QStringLiteral("label"),
                         text(element.value(QStringLiteral("label")), id + QStringLiteral(".label"), 1000));
            props.insert(QStringLiteral("shape"),
                         optionalEnum(element.value(QStringLiteral("shape")), shapeNames(), QStringLiteral("rounded"),
                                      id + QStringLiteral(".shape")));
            model.nodes.append({QStringLiteral("freeNode"), id, props});
        } else if (type.toString() == QLatin1String("frame")) {
            box(element, id, props);
            props.insert(QStringLiteral("title"),
                         text(orDefault(element.value(QStringLiteral("title")), element.value(QStringLiteral("label"))),
                              id + QStringLiteral(".title"), 500));
            model.nodes.append({QStringLiteral("freeFrame"), id, props});
        } else if (type.toString() == QLatin1String("stroke")) {
            const QJsonValue pointsValue = element.value(QStringLiteral("points"));
            const QJsonArray rawPoints = pointsValue.toArray();
            if (!pointsValue.isArray() || rawPoints.size() < 2 || rawPoints.size() > maxPoints)
                fail(QStringLiteral("%1.points must contain 2 to %2 points").arg(id).arg(maxPoints));
            QJsonArray points;
            for (int i = 0; i < rawPoints.size(); ++i)
                points.append(strokePoint(rawPoints[i], id, i));
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
        } else {
            pendingEdges.append({element, {id, props}});
        }
    }

    // Membership: a parent is another frame, and the chain never loops.
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
        QSet<QString> seen{node.key};
        for (QString up = parent; !up.isEmpty(); up = parents.value(up)) {
            if (seen.contains(up))
                fail(node.key + QStringLiteral(".parent creates a frame cycle"));
            seen.insert(up);
        }
    }

    QSet<QString> visible;
    for (const Model::Node &node : model.nodes)
        visible.insert(node.key);
    for (const auto &[element, shared] : pendingEdges) {
        const auto &[id, common] = shared;
        const QString from = endpoint(element.value(QStringLiteral("from")), id, QStringLiteral("from"), model);
        const QString to = endpoint(element.value(QStringLiteral("to")), id, QStringLiteral("to"), model);
        for (const QString &end : {from, to}) {
            if (!end.startsWith(QLatin1String("__")) && !visible.contains(end))
                fail(QStringLiteral("%1 references unknown element \"%2\"").arg(id, end));
        }
        QJsonObject props = common;
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

    model.meta = QJsonObject{{QStringLiteral("canvas"), validateCanvas(document.value(QStringLiteral("canvas")))},
                             {QStringLiteral("steps"), validateSteps(document.value(QStringLiteral("steps")))},
                             {QStringLiteral("order"), order}};
}

// ---------------------------------------------------------------------------
// Writing: JSON.stringify(document, null, 2), keys in sidecar's order, so
// that a board saved by either app reads (and diffs) the same.

QString quote(const QString &value)
{
    QString out = QStringLiteral("\"");
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value[i];
        switch (c.unicode()) {
        case '"':
            out += QStringLiteral("\\\"");
            break;
        case '\\':
            out += QStringLiteral("\\\\");
            break;
        case '\b':
            out += QStringLiteral("\\b");
            break;
        case '\f':
            out += QStringLiteral("\\f");
            break;
        case '\n':
            out += QStringLiteral("\\n");
            break;
        case '\r':
            out += QStringLiteral("\\r");
            break;
        case '\t':
            out += QStringLiteral("\\t");
            break;
        default:
            if (c.unicode() < 0x20) {
                out += QStringLiteral("\\u%1").arg(uint(c.unicode()), 4, 16, QLatin1Char('0'));
            } else if (c.isSurrogate()) {
                // A lone surrogate is escaped, a pair is written as is.
                const bool pair = c.isHighSurrogate() && i + 1 < value.size() && value[i + 1].isLowSurrogate();
                if (pair) {
                    out += c;
                    out += value[++i];
                } else {
                    out += QStringLiteral("\\u%1").arg(uint(c.unicode()), 4, 16, QLatin1Char('0'));
                }
            } else {
                out += c;
            }
        }
    }
    return out + QLatin1Char('"');
}

QString numberText(qreal value)
{
    if (!std::isfinite(value))
        return QStringLiteral("null");
    if (value == std::floor(value) && std::abs(value) < 1e15)
        return QString::number(qint64(value));
    return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

QString write(const QJsonValue &value, int depth, const QStringList &order = {});

QString writeObject(const QJsonObject &object, int depth, const QStringList &order)
{
    if (object.isEmpty())
        return QStringLiteral("{}");
    static const QStringList nested{QStringLiteral("id"),     QStringLiteral("title"),   QStringLiteral("narration"),
                                    QStringLiteral("x"),      QStringLiteral("y"),       QStringLiteral("width"),
                                    QStringLiteral("height"), QStringLiteral("element"), QStringLiteral("point")};
    QStringList keys;
    for (const QStringList &list : {order, nested}) {
        for (const QString &key : list) {
            if (object.contains(key) && !keys.contains(key))
                keys.append(key);
        }
    }
    for (const QString &key : object.keys()) {
        if (!keys.contains(key))
            keys.append(key);
    }
    const QString inner(2 * (depth + 1), QLatin1Char(' '));
    QStringList parts;
    for (const QString &key : keys)
        parts.append(inner + quote(key) + QStringLiteral(": ") + write(object.value(key), depth + 1));
    return QStringLiteral("{\n") + parts.join(QStringLiteral(",\n")) + QLatin1Char('\n') +
           QString(2 * depth, QLatin1Char(' ')) + QLatin1Char('}');
}

QString write(const QJsonValue &value, int depth, const QStringList &order)
{
    switch (value.type()) {
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double:
        return numberText(value.toDouble());
    case QJsonValue::String:
        return quote(value.toString());
    case QJsonValue::Array: {
        const QJsonArray array = value.toArray();
        if (array.isEmpty())
            return QStringLiteral("[]");
        const QString inner(2 * (depth + 1), QLatin1Char(' '));
        QStringList parts;
        for (const QJsonValue &item : array)
            parts.append(inner + write(item, depth + 1, order));
        return QStringLiteral("[\n") + parts.join(QStringLiteral(",\n")) + QLatin1Char('\n') +
               QString(2 * depth, QLatin1Char(' ')) + QLatin1Char(']');
    }
    case QJsonValue::Object:
        return writeObject(value.toObject(), depth, order);
    default:
        return QStringLiteral("null");
    }
}

// The key order of each element type, as sidecar builds them.
const QStringList &elementOrder(const QString &type)
{
    static const QStringList tail{QStringLiteral("description"), QStringLiteral("tone"), QStringLiteral("appearAt"),
                                  QStringLiteral("fill"), QStringLiteral("parent")};
    static const QStringList node =
        QStringList{QStringLiteral("id"), QStringLiteral("type"),  QStringLiteral("shape"),  QStringLiteral("x"),
                    QStringLiteral("y"),  QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("label")} +
        tail;
    static const QStringList frame =
        QStringList{QStringLiteral("id"),    QStringLiteral("type"),   QStringLiteral("x"),    QStringLiteral("y"),
                    QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("title")} +
        tail;
    static const QStringList stroke =
        QStringList{QStringLiteral("id"), QStringLiteral("type"), QStringLiteral("x"), QStringLiteral("y"),
                    QStringLiteral("points")} +
        tail + QStringList{QStringLiteral("label"), QStringLiteral("arrow"), QStringLiteral("line")};
    static const QStringList edge =
        QStringList{QStringLiteral("id"), QStringLiteral("type"), QStringLiteral("from"), QStringLiteral("to")} + tail +
        QStringList{QStringLiteral("label"), QStringLiteral("arrow"), QStringLiteral("line")};
    if (type == QLatin1String("node"))
        return node;
    if (type == QLatin1String("frame"))
        return frame;
    if (type == QLatin1String("stroke"))
        return stroke;
    return edge;
}

qreal cleanNumber(qreal value)
{
    return std::round(value * 100) / 100;
}

void optionalCommon(QJsonObject &target,
                    const QString &description,
                    const QString &tone,
                    int appearAt,
                    const QString &fill,
                    const QString &parent)
{
    if (!description.isEmpty())
        target.insert(QStringLiteral("description"), description);
    if (!tone.isEmpty() && tone != QLatin1String("neutral"))
        target.insert(QStringLiteral("tone"), tone);
    if (appearAt)
        target.insert(QStringLiteral("appearAt"), appearAt);
    if (!fill.isEmpty())
        target.insert(QStringLiteral("fill"), fill);
    if (!parent.isEmpty())
        target.insert(QStringLiteral("parent"), parent);
}

bool isPublic(const Shape &shape)
{
    return shape.type == QLatin1String("freeNode") || shape.type == QLatin1String("freeFrame") ||
           shape.type == QLatin1String("freeStroke");
}

class ExplainerBoard : public Codec {
public:
    QString id() const override { return QStringLiteral("explainer-board"); }

    QString language() const override { return QStringLiteral("board"); }

    QString title() const override { return QStringLiteral("Explainer board"); }

    const Family &family() const override { return families::explainer(); }

    // The scene carries its coordinates: they are source data, never laid
    // out again (only elements written without a place are).
    bool arrangeWithGraphviz() const override { return false; }

    bool sourceOwnsLayout() const override { return true; }

    bool matches(const QString &source) const override
    {
        static const QRegularExpression pattern(QStringLiteral("^\\s*\\{[\\s\\S]*\"format\"\\s*:\\s*\"sciilo-board\""));
        return pattern.match(source).hasMatch();
    }

    bool parse(const QString &source, Model &model, QString *error) const override
    {
        try {
            parseScene(source, model);
        } catch (const Invalid &invalid) {
            model = Model();
            if (error)
                *error = invalid.message;
            return false;
        }
        return true;
    }

    QString serialize(const Board &board, const QJsonObject &meta) const override
    {
        // The element id of each shape and link: its source key, else its
        // board id, made unique and safe so that the saved scene always reads
        // back (a copy, a shape created here, an id that collides).
        QSet<QString> used;
        const auto claim = [&used](const QString &key, const QString &id) {
            QString candidate = safeId(key) ? key : id;
            if (!safeId(candidate))
                candidate = QStringLiteral("e");
            const QString root = candidate;
            for (int index = 2; used.contains(candidate); ++index)
                candidate = root.left(70) + QLatin1Char('_') + QString::number(index);
            used.insert(candidate);
            return candidate;
        };
        QHash<const Shape *, QString> shapeIds;
        for (const auto &s : board.shapes()) {
            if (isPublic(*s))
                shapeIds.insert(s.get(), claim(s->key, s->id));
        }
        // Membership: only another frame, and never a loop, or the scene
        // would be refused on the next read.
        QHash<QString, QString> parents;
        for (const auto &s : board.shapes()) {
            if (!shapeIds.contains(s.get()) || s->parentBoundary.isEmpty())
                continue;
            const Shape *parent = board.shapeByReference(s->parentBoundary);
            if (parent && parent != s.get() && parent->type == QLatin1String("freeFrame"))
                parents.insert(shapeIds.value(s.get()), shapeIds.value(parent));
        }
        for (const auto &s : board.shapes()) {
            const QString id = shapeIds.value(s.get());
            QSet<QString> seen{id};
            for (QString up = parents.value(id); !up.isEmpty(); up = parents.value(up)) {
                if (seen.contains(up)) {
                    parents.remove(id);
                    break;
                }
                seen.insert(up);
            }
        }

        QList<std::pair<QString, QJsonObject>> byId;
        for (const auto &s : board.shapes()) {
            if (!shapeIds.contains(s.get()))
                continue;
            const QString id = shapeIds.value(s.get());
            const QString parent = parents.value(id);
            QJsonObject element{{QStringLiteral("id"), id}};
            if (const auto *node = dynamic_cast<const FreeNodeShape *>(s.get())) {
                element.insert(QStringLiteral("type"), QStringLiteral("node"));
                element.insert(QStringLiteral("shape"), node->shape);
                element.insert(QStringLiteral("x"), cleanNumber(node->x));
                element.insert(QStringLiteral("y"), cleanNumber(node->y));
                element.insert(QStringLiteral("width"), cleanNumber(node->w));
                element.insert(QStringLiteral("height"), cleanNumber(node->h));
                element.insert(QStringLiteral("label"), node->label);
                optionalCommon(element, node->description, node->tone, node->appearAt, node->customFill, parent);
            } else if (const auto *frame = dynamic_cast<const FreeFrameShape *>(s.get())) {
                element.insert(QStringLiteral("type"), QStringLiteral("frame"));
                element.insert(QStringLiteral("x"), cleanNumber(frame->x));
                element.insert(QStringLiteral("y"), cleanNumber(frame->y));
                element.insert(QStringLiteral("width"), cleanNumber(frame->w));
                element.insert(QStringLiteral("height"), cleanNumber(frame->h));
                element.insert(QStringLiteral("title"), frame->title);
                optionalCommon(element, frame->description, frame->tone, frame->appearAt, frame->customFill, parent);
            } else if (const auto *stroke = dynamic_cast<const FreeStrokeShape *>(s.get())) {
                element.insert(QStringLiteral("type"), QStringLiteral("stroke"));
                element.insert(QStringLiteral("x"), cleanNumber(stroke->x));
                element.insert(QStringLiteral("y"), cleanNumber(stroke->y));
                QJsonArray points;
                for (const QPointF &p : stroke->points)
                    points.append(QJsonArray{cleanNumber(p.x()), cleanNumber(p.y())});
                element.insert(QStringLiteral("points"), points);
                optionalCommon(element, stroke->description, stroke->tone, stroke->appearAt, stroke->customFill,
                               parent);
                if (!stroke->label.isEmpty())
                    element.insert(QStringLiteral("label"), stroke->label);
                if (stroke->arrow != QLatin1String("none"))
                    element.insert(QStringLiteral("arrow"), stroke->arrow);
                if (stroke->line != QLatin1String("solid"))
                    element.insert(QStringLiteral("line"), stroke->line);
            } else {
                continue;
            }
            byId.append({id, element});
        }
        const auto endpointSource = [&](const QString &shapeId) -> std::optional<QJsonValue> {
            const Shape *s = board.shape(shapeId);
            if (!s)
                return std::nullopt;
            // A free point: the hidden anchor's center.
            if (s->type == QLatin1String("freeAnchor"))
                return QJsonValue(QJsonObject{{QStringLiteral("x"), cleanNumber(s->cx())},
                                              {QStringLiteral("y"), cleanNumber(s->cy())}});
            if (!shapeIds.contains(s))
                return std::nullopt;
            return QJsonValue(shapeIds.value(s));
        };
        for (const auto &l : board.links()) {
            const std::optional<QJsonValue> from = endpointSource(l->from);
            const std::optional<QJsonValue> to = endpointSource(l->to);
            if (!from || !to)
                continue;
            const QString id = claim(l->key, l->id);
            const auto *edge = dynamic_cast<const FreeEdge *>(l.get());
            QJsonObject element{{QStringLiteral("id"), id},
                                {QStringLiteral("type"), QStringLiteral("edge")},
                                {QStringLiteral("from"), *from},
                                {QStringLiteral("to"), *to}};
            if (edge)
                optionalCommon(element, edge->description, edge->tone, edge->appearAt, {}, {});
            if (!l->label.isEmpty())
                element.insert(QStringLiteral("label"), l->label);
            const QString arrow = edge ? edge->arrow : QStringLiteral("end");
            if (arrow != QLatin1String("end"))
                element.insert(QStringLiteral("arrow"), arrow);
            if (!l->customLineStyle.isEmpty())
                element.insert(QStringLiteral("line"), l->customLineStyle);
            byId.append({id, element});
        }

        // The source's order first, what is new after it.
        QList<QJsonObject> ordered;
        QSet<QString> placed;
        for (const QJsonValue &value : meta.value(QStringLiteral("order")).toArray()) {
            const QString id = value.toString();
            for (const auto &[elementId, element] : byId) {
                if (elementId == id && !placed.contains(id)) {
                    ordered.append(element);
                    placed.insert(id);
                    break;
                }
            }
        }
        for (const auto &[elementId, element] : byId) {
            if (!placed.contains(elementId))
                ordered.append(element);
        }

        const QJsonValue canvas =
            meta.value(QStringLiteral("canvas")).isObject()
                ? meta.value(QStringLiteral("canvas"))
                : QJsonValue(QJsonObject{{QStringLiteral("width"), 1200}, {QStringLiteral("height"), 800}});
        QStringList elements;
        for (const QJsonObject &element : ordered)
            elements.append(QStringLiteral("    ") +
                            write(element, 2, elementOrder(element.value(QStringLiteral("type")).toString())));
        QStringList parts{QStringLiteral("  \"format\": ") + quote(format),
                          QStringLiteral("  \"version\": ") + QString::number(version),
                          QStringLiteral("  \"canvas\": ") + write(canvas, 1),
                          QStringLiteral("  \"elements\": ") +
                              (elements.isEmpty() ? QStringLiteral("[]")
                                                  : QStringLiteral("[\n") + elements.join(QStringLiteral(",\n")) +
                                                        QStringLiteral("\n  ]"))};
        const QJsonArray steps = meta.value(QStringLiteral("steps")).toArray();
        if (!steps.isEmpty())
            parts.append(QStringLiteral("  \"steps\": ") + write(steps, 1));
        return QStringLiteral("{\n") + parts.join(QStringLiteral(",\n")) + QStringLiteral("\n}");
    }

    QString blank() const override
    {
        return QStringLiteral("{\n  \"format\": \"sciilo-board\",\n  \"version\": 1,\n  \"canvas\": {\n"
                              "    \"width\": 1200,\n    \"height\": 800\n  },\n  \"elements\": []\n}");
    }
};

} // namespace

} // namespace diagram::explainer

namespace diagram::families {

QList<const Codec *> explainerCodecs()
{
    static const explainer::ExplainerBoard board;
    return {&board};
}

} // namespace diagram::families
