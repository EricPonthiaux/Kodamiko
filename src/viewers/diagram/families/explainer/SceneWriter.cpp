// Writing an explainer scene: JSON.stringify(document, null, 2), keys in
// sidecar's order, so that a board saved by either app reads (and diffs)
// the same.

#include "SceneWriter.h"

#include "Explainer.h"
#include "SceneFormat.h"
#include "viewers/diagram/Board.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <cmath>
#include <optional>
#include <utility>

namespace diagram::explainer {

namespace {

QString unicodeEscape(QChar c)
{
    return QStringLiteral("\\u%1").arg(uint(c.unicode()), 4, 16, QLatin1Char('0'));
}

// The escape of a character that needs one whatever its neighbors, if any.
std::optional<QString> fixedEscape(QChar c)
{
    switch (c.unicode()) {
    case '"':
        return QStringLiteral("\\\"");
    case '\\':
        return QStringLiteral("\\\\");
    case '\b':
        return QStringLiteral("\\b");
    case '\f':
        return QStringLiteral("\\f");
    case '\n':
        return QStringLiteral("\\n");
    case '\r':
        return QStringLiteral("\\r");
    case '\t':
        return QStringLiteral("\\t");
    default:
        break;
    }
    if (c.unicode() < 0x20)
        return unicodeEscape(c);
    return std::nullopt;
}

QString quote(const QString &value)
{
    QString out = QStringLiteral("\"");
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value[i];
        if (const std::optional<QString> escape = fixedEscape(c)) {
            out += *escape;
            continue;
        }
        if (!c.isSurrogate()) {
            out += c;
            continue;
        }
        // A lone surrogate is escaped, a pair is written as is.
        const bool pair = c.isHighSurrogate() && i + 1 < value.size() && value[i + 1].isLowSurrogate();
        if (!pair) {
            out += unicodeEscape(c);
            continue;
        }
        out += c;
        out += value[++i];
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
    const QString inner(qsizetype(depth + 1) * 2, QLatin1Char(' '));
    QStringList parts;
    for (const QString &key : keys)
        parts.append(inner + quote(key) + QStringLiteral(": ") + write(object.value(key), depth + 1));
    return QStringLiteral("{\n") + parts.join(QStringLiteral(",\n")) + QLatin1Char('\n') +
           QString(qsizetype(depth) * 2, QLatin1Char(' ')) + QLatin1Char('}');
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
        const QString inner(qsizetype(depth + 1) * 2, QLatin1Char(' '));
        QStringList parts;
        for (const QJsonValue &item : array)
            parts.append(inner + write(item, depth + 1, order));
        return QStringLiteral("[\n") + parts.join(QStringLiteral(",\n")) + QLatin1Char('\n') +
               QString(qsizetype(depth) * 2, QLatin1Char(' ')) + QLatin1Char(']');
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

// The optional properties every element shares, written only when set.
struct CommonProps {
    QString description;
    QString tone;
    int appearAt = 0;
    QString fill;
    QString parent;
};

void insertCommon(QJsonObject &target, const CommonProps &props)
{
    if (!props.description.isEmpty())
        target.insert(QStringLiteral("description"), props.description);
    if (!props.tone.isEmpty() && props.tone != QLatin1String("neutral"))
        target.insert(QStringLiteral("tone"), props.tone);
    if (props.appearAt)
        target.insert(QStringLiteral("appearAt"), props.appearAt);
    if (!props.fill.isEmpty())
        target.insert(QStringLiteral("fill"), props.fill);
    if (!props.parent.isEmpty())
        target.insert(QStringLiteral("parent"), props.parent);
}

bool isPublic(const Shape &shape)
{
    return shape.type == QLatin1String("freeNode") || shape.type == QLatin1String("freeFrame") ||
           shape.type == QLatin1String("freeStroke");
}

// The element ids of a saved scene: a source key, else a board id, made
// unique and safe so that the scene always reads back (a copy, a shape
// created here, an id that collides).
class IdClaims {
public:
    QString claim(const QString &key, const QString &boardId)
    {
        QString candidate = isSafeId(key) ? key : boardId;
        if (!isSafeId(candidate))
            candidate = QStringLiteral("e");
        const QString root = candidate;
        for (int index = 2; used_.contains(candidate); ++index)
            candidate = root.left(70) + QLatin1Char('_') + QString::number(index);
        used_.insert(candidate);
        return candidate;
    }

private:
    QSet<QString> used_;
};

using ShapeIds = QHash<const Shape *, QString>;

ShapeIds claimShapeIds(const Board &board, IdClaims &claims)
{
    ShapeIds ids;
    for (const auto &s : board.shapes()) {
        if (isPublic(*s))
            ids.insert(s.get(), claims.claim(s->key, s->id));
    }
    return ids;
}

// Whether the parent chain from `id` comes back to an element already seen.
bool reachesCycle(const QString &id, const QHash<QString, QString> &parents)
{
    QSet<QString> seen{id};
    for (QString up = parents.value(id); !up.isEmpty(); up = parents.value(up)) {
        if (seen.contains(up))
            return true;
        seen.insert(up);
    }
    return false;
}

// Membership: only another frame, and never a loop, or the scene would be
// refused on the next read.
QHash<QString, QString> frameParents(const Board &board, const ShapeIds &shapeIds)
{
    QHash<QString, QString> parents;
    for (const auto &s : board.shapes()) {
        if (!shapeIds.contains(s.get()) || s->parentBoundary.isEmpty())
            continue;
        const Shape *parent = board.shapeByReference(s->parentBoundary);
        if (parent && parent != s.get() && parent->type == QLatin1String("freeFrame"))
            parents.insert(shapeIds.value(s.get()), shapeIds.value(parent));
    }
    // In board order, each break seen by the shapes after it.
    for (const auto &s : board.shapes()) {
        const QString id = shapeIds.value(s.get());
        if (reachesCycle(id, parents))
            parents.remove(id);
    }
    return parents;
}

void insertBox(QJsonObject &element, const Shape &shape)
{
    element.insert(QStringLiteral("x"), cleanNumber(shape.x));
    element.insert(QStringLiteral("y"), cleanNumber(shape.y));
    element.insert(QStringLiteral("width"), cleanNumber(shape.w));
    element.insert(QStringLiteral("height"), cleanNumber(shape.h));
}

QJsonObject nodeElement(const FreeNodeShape &node, const QString &parent)
{
    QJsonObject element{{QStringLiteral("type"), QStringLiteral("node")}, {QStringLiteral("shape"), node.shape}};
    insertBox(element, node);
    element.insert(QStringLiteral("label"), node.label);
    insertCommon(element, {.description = node.description,
                           .tone = node.tone,
                           .appearAt = node.appearAt,
                           .fill = node.customFill,
                           .parent = parent});
    return element;
}

QJsonObject frameElement(const FreeFrameShape &frame, const QString &parent)
{
    QJsonObject element{{QStringLiteral("type"), QStringLiteral("frame")}};
    insertBox(element, frame);
    element.insert(QStringLiteral("title"), frame.title);
    insertCommon(element, {.description = frame.description,
                           .tone = frame.tone,
                           .appearAt = frame.appearAt,
                           .fill = frame.customFill,
                           .parent = parent});
    return element;
}

QJsonObject strokeElement(const FreeStrokeShape &stroke, const QString &parent)
{
    QJsonObject element{{QStringLiteral("type"), QStringLiteral("stroke")},
                        {QStringLiteral("x"), cleanNumber(stroke.x)},
                        {QStringLiteral("y"), cleanNumber(stroke.y)}};
    QJsonArray points;
    for (const QPointF &p : stroke.points)
        points.append(QJsonArray{cleanNumber(p.x()), cleanNumber(p.y())});
    element.insert(QStringLiteral("points"), points);
    insertCommon(element, {.description = stroke.description,
                           .tone = stroke.tone,
                           .appearAt = stroke.appearAt,
                           .fill = stroke.customFill,
                           .parent = parent});
    if (!stroke.label.isEmpty())
        element.insert(QStringLiteral("label"), stroke.label);
    if (stroke.arrow != QLatin1String("none"))
        element.insert(QStringLiteral("arrow"), stroke.arrow);
    if (stroke.line != QLatin1String("solid"))
        element.insert(QStringLiteral("line"), stroke.line);
    return element;
}

// A public shape's element, without its id; none for the others.
std::optional<QJsonObject> shapeElement(const Shape &shape, const QString &parent)
{
    if (const auto *node = dynamic_cast<const FreeNodeShape *>(&shape))
        return nodeElement(*node, parent);
    if (const auto *frame = dynamic_cast<const FreeFrameShape *>(&shape))
        return frameElement(*frame, parent);
    if (const auto *stroke = dynamic_cast<const FreeStrokeShape *>(&shape))
        return strokeElement(*stroke, parent);
    return std::nullopt;
}

using IdentifiedElements = QList<std::pair<QString, QJsonObject>>;

IdentifiedElements shapeElements(const Board &board, const ShapeIds &shapeIds)
{
    const QHash<QString, QString> parents = frameParents(board, shapeIds);
    IdentifiedElements elements;
    for (const auto &s : board.shapes()) {
        if (!shapeIds.contains(s.get()))
            continue;
        const QString id = shapeIds.value(s.get());
        std::optional<QJsonObject> element = shapeElement(*s, parents.value(id));
        if (!element)
            continue;
        element->insert(QStringLiteral("id"), id);
        elements.append({id, *element});
    }
    return elements;
}

// An edge's end in the scene: an element's id, or a free point (the hidden
// anchor's center); none for an end the scene does not hold.
std::optional<QJsonValue> endpointSource(const Board &board, const ShapeIds &shapeIds, const QString &shapeId)
{
    const Shape *s = board.shape(shapeId);
    if (!s)
        return std::nullopt;
    if (s->type == QLatin1String("freeAnchor"))
        return QJsonValue(
            QJsonObject{{QStringLiteral("x"), cleanNumber(s->cx())}, {QStringLiteral("y"), cleanNumber(s->cy())}});
    if (!shapeIds.contains(s))
        return std::nullopt;
    return QJsonValue(shapeIds.value(s));
}

// A link's element, without its id.
QJsonObject edgeElement(const Link &link, const QJsonValue &from, const QJsonValue &to)
{
    const auto *edge = dynamic_cast<const FreeEdge *>(&link);
    QJsonObject element{
        {QStringLiteral("type"), QStringLiteral("edge")}, {QStringLiteral("from"), from}, {QStringLiteral("to"), to}};
    if (edge)
        insertCommon(element, {.description = edge->description, .tone = edge->tone, .appearAt = edge->appearAt});
    if (!link.label.isEmpty())
        element.insert(QStringLiteral("label"), link.label);
    const QString arrow = edge ? edge->arrow : QStringLiteral("end");
    if (arrow != QLatin1String("end"))
        element.insert(QStringLiteral("arrow"), arrow);
    if (!link.customLineStyle.isEmpty())
        element.insert(QStringLiteral("line"), link.customLineStyle);
    return element;
}

IdentifiedElements edgeElements(const Board &board, const ShapeIds &shapeIds, IdClaims &claims)
{
    IdentifiedElements elements;
    for (const auto &l : board.links()) {
        const std::optional<QJsonValue> from = endpointSource(board, shapeIds, l->from);
        const std::optional<QJsonValue> to = endpointSource(board, shapeIds, l->to);
        if (!from || !to)
            continue;
        const QString id = claims.claim(l->key, l->id);
        QJsonObject element = edgeElement(*l, *from, *to);
        element.insert(QStringLiteral("id"), id);
        elements.append({id, element});
    }
    return elements;
}

// The source's order first, what is new after it.
QList<QJsonObject> inSourceOrder(const IdentifiedElements &elements, const QJsonArray &order)
{
    QList<QJsonObject> ordered;
    QSet<QString> placed;
    for (const QJsonValue &value : order) {
        const QString id = value.toString();
        if (placed.contains(id))
            continue;
        const auto match = std::ranges::find_if(elements, [&id](const auto &entry) { return entry.first == id; });
        if (match == elements.end())
            continue;
        ordered.append(match->second);
        placed.insert(id);
    }
    for (const auto &[elementId, element] : elements) {
        if (!placed.contains(elementId))
            ordered.append(element);
    }
    return ordered;
}

QString sceneText(const QList<QJsonObject> &ordered, const QJsonObject &meta)
{
    const QJsonValue canvas =
        meta.value(QStringLiteral("canvas")).isObject()
            ? meta.value(QStringLiteral("canvas"))
            : QJsonValue(QJsonObject{{QStringLiteral("width"), 1200}, {QStringLiteral("height"), 800}});
    QStringList elements;
    for (const QJsonObject &element : ordered)
        elements.append(QStringLiteral("    ") +
                        write(element, 2, elementOrder(element.value(QStringLiteral("type")).toString())));
    const QString elementsText =
        elements.isEmpty() ? QStringLiteral("[]")
                           : QStringLiteral("[\n") + elements.join(QStringLiteral(",\n")) + QStringLiteral("\n  ]");
    QStringList parts{QStringLiteral("  \"format\": ") + quote(sceneFormat),
                      QStringLiteral("  \"version\": ") + QString::number(sceneVersion),
                      QStringLiteral("  \"canvas\": ") + write(canvas, 1),
                      QStringLiteral("  \"elements\": ") + elementsText};
    const QJsonArray steps = meta.value(QStringLiteral("steps")).toArray();
    if (!steps.isEmpty())
        parts.append(QStringLiteral("  \"steps\": ") + write(steps, 1));
    return QStringLiteral("{\n") + parts.join(QStringLiteral(",\n")) + QStringLiteral("\n}");
}
} // namespace

QString writeScene(const Board &board, const QJsonObject &meta)
{
    IdClaims claims;
    const ShapeIds shapeIds = claimShapeIds(board, claims);
    IdentifiedElements elements = shapeElements(board, shapeIds);
    elements.append(edgeElements(board, shapeIds, claims));
    return sceneText(inSourceOrder(elements, meta.value(QStringLiteral("order")).toArray()), meta);
}

} // namespace diagram::explainer
