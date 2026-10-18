// The relations of a data model: which FK column joins which referenced
// column, and the orthogonal path drawn between their rows.

#include "DataModel.h"
#include "References.h"
#include "viewers/diagram/Board.h"

#include <QCoreApplication>
#include <QPainter>

#include <cmath>
#include <optional>
#include <utility>

namespace diagram::datamodel {

using namespace detail;

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("DataModelDiagram", text);
}

// The FK side `owner` and the column of `referenced` it points to: the one
// chosen by hand, else the one its comment names, else the primary column.
RelationshipLink::Endpoints endpointFor(const EntityShape &owner,
                                        const EntityShape &referenced,
                                        const Field &foreignKey,
                                        const Field *target)
{
    if (!target) {
        if (const std::optional<Reference> reference = referencedColumn(foreignKey.comment))
            target = referenced.column(reference->column);
    }
    if (!target)
        target = referenced.primaryColumn();
    return {&owner, &referenced, foreignKey.name, target ? target->name : QString()};
}

// Cardinalities as Mermaid writes them on each side of `--`.
const QList<std::pair<QString, QString>> &fromCardChoices()
{
    static const QList<std::pair<QString, QString>> list{
        {QStringLiteral("||"), tr("Exactly one")},
        {QStringLiteral("|o"), tr("Zero or one")},
        {QStringLiteral("}|"), tr("One or more")},
        {QStringLiteral("}o"), tr("Zero or more")},
    };
    return list;
}

const QList<std::pair<QString, QString>> &toCardChoices()
{
    static const QList<std::pair<QString, QString>> list{
        {QStringLiteral("||"), tr("Exactly one")},
        {QStringLiteral("o|"), tr("Zero or one")},
        {QStringLiteral("|{"), tr("One or more")},
        {QStringLiteral("o{"), tr("Zero or more")},
    };
    return list;
}

// The choices, with the current value kept when the source wrote another
// spelling.
QList<std::pair<QString, QString>> withValue(QList<std::pair<QString, QString>> choices, const QString &value)
{
    const bool known = std::any_of(choices.begin(), choices.end(), [&](const auto &c) { return c.first == value; });
    if (!known && !value.isEmpty())
        choices.prepend({value, value});
    return choices;
}

} // namespace

// ---------------------------------------------------------------------------
// RelationshipLink

RelationshipLink::RelationshipLink()
    : Link(QStringLiteral("relationship"))
{
}

void RelationshipLink::load(const QJsonObject &props)
{
    Link::load(props);
    fromCard = jsonString(props, QStringLiteral("fromCard"), QStringLiteral("||"));
    toCard = jsonString(props, QStringLiteral("toCard"), QStringLiteral("o{"));
    fromColumn = jsonString(props, QStringLiteral("fromColumn"), jsonString(props, QStringLiteral("fromPort")));
    toColumn = jsonString(props, QStringLiteral("toColumn"), jsonString(props, QStringLiteral("toPort")));
    identifying = jsonBool(props, QStringLiteral("identifying"), true);
}

QJsonObject RelationshipLink::toJson() const
{
    QJsonObject json = Link::toJson();
    json.insert(QStringLiteral("fromCard"), fromCard);
    json.insert(QStringLiteral("toCard"), toCard);
    if (!fromColumn.isEmpty())
        json.insert(QStringLiteral("fromColumn"), fromColumn);
    if (!toColumn.isEmpty())
        json.insert(QStringLiteral("toColumn"), toColumn);
    if (!identifying)
        json.insert(QStringLiteral("identifying"), false);
    return json;
}

RelationshipLink::Endpoints RelationshipLink::relationshipEndpoints(const EntityShape &fromShape,
                                                                    const EntityShape &toShape) const
{
    const Field *selectedFrom = fromShape.column(fromColumn);
    const Field *selectedTo = toShape.column(toColumn);
    // Columns chosen by hand: their FK decides the direction, the FK being
    // always the source of the arrow.
    if (selectedFrom && selectedFrom->has("FK"))
        return endpointFor(fromShape, toShape, *selectedFrom, selectedTo);
    if (selectedTo && selectedTo->has("FK"))
        return endpointFor(toShape, fromShape, *selectedTo, selectedFrom);
    return foreignKeyEndpoints(fromShape, toShape, selectedFrom, selectedTo);
}

RelationshipLink::Endpoints RelationshipLink::foreignKeyEndpoints(const EntityShape &fromShape,
                                                                  const EntityShape &toShape,
                                                                  const Field *selectedFrom,
                                                                  const Field *selectedTo) const
{
    const std::optional<EntityShape::ForeignKey> fromForeignKey = fromShape.foreignKeyTo(toShape);
    const std::optional<EntityShape::ForeignKey> toForeignKey = toShape.foreignKeyTo(fromShape);
    // An explicit target column goes with the FK found in the other table
    // (a natural key or a UK, not only `id`).
    if (selectedFrom && toForeignKey)
        return endpointFor(toShape, fromShape, *toForeignKey->field, selectedFrom);
    if (selectedTo && fromForeignKey)
        return endpointFor(fromShape, toShape, *fromForeignKey->field, selectedTo);

    if (fromForeignKey) {
        const bool manyAtStart = fromCard.contains(QLatin1Char('}'));
        const bool fromKeyWins = !toForeignKey || fromForeignKey->score > toForeignKey->score || manyAtStart;
        if (fromKeyWins)
            return endpointFor(fromShape, toShape, *fromForeignKey->field, nullptr);
    }
    if (toForeignKey)
        return endpointFor(toShape, fromShape, *toForeignKey->field, nullptr);

    const Field *fromPrimary = selectedFrom ? selectedFrom : fromShape.primaryColumn();
    const Field *toPrimary = selectedTo ? selectedTo : toShape.primaryColumn();
    return {&fromShape, &toShape, fromPrimary ? fromPrimary->name : QString(), toPrimary ? toPrimary->name : QString()};
}

std::optional<RelationshipLink::ColumnEndpoints> RelationshipLink::columnEndpoints(const Board &board) const
{
    const auto *fromShape = dynamic_cast<const EntityShape *>(board.shape(from));
    const auto *toShape = dynamic_cast<const EntityShape *>(board.shape(to));
    if (!fromShape || !toShape)
        return std::nullopt;
    const Endpoints relationship = relationshipEndpoints(*fromShape, *toShape);
    if (relationship.fromColumn.isEmpty() || relationship.toColumn.isEmpty())
        return std::nullopt;
    const bool leftToRight = relationship.toShape->cx() >= relationship.fromShape->cx();
    const std::optional<QPointF> a = relationship.fromShape->columnAnchor(relationship.fromColumn, !leftToRight);
    const std::optional<QPointF> b = relationship.toShape->columnAnchor(relationship.toColumn, leftToRight);
    if (!a || !b)
        return std::nullopt;
    return ColumnEndpoints{relationship.fromShape,
                           relationship.toShape,
                           {relationship.fromColumn, !leftToRight, *a},
                           {relationship.toColumn, leftToRight, *b}};
}

std::optional<std::pair<GraphvizEndpoint, GraphvizEndpoint>> RelationshipLink::graphvizEndpoints(
    const Board &board,
    const QString &rankdir) const
{
    const auto *fromShape = dynamic_cast<const EntityShape *>(board.shape(from));
    const auto *toShape = dynamic_cast<const EntityShape *>(board.shape(to));
    if (!fromShape || !toShape)
        return std::nullopt;
    const Endpoints relationship = relationshipEndpoints(*fromShape, *toShape);
    const QString fromPort = relationship.fromShape->graphvizPortForColumn(relationship.fromColumn);
    const QString toPort = relationship.toShape->graphvizPortForColumn(relationship.toColumn);
    if (fromPort.isEmpty() || toPort.isEmpty())
        return std::nullopt;
    const bool reverse = rankdir == QLatin1String("RL");
    return std::pair{
        GraphvizEndpoint{relationship.fromShape->id, fromPort, reverse ? QStringLiteral("w") : QStringLiteral("e")},
        GraphvizEndpoint{relationship.toShape->id, toPort, reverse ? QStringLiteral("e") : QStringLiteral("w")}};
}

std::optional<Route> RelationshipLink::route(const Board &board) const
{
    const std::optional<ColumnEndpoints> ends = columnEndpoints(board);
    if (!ends)
        return Link::route(board);
    // Without a stored spline the standard route brings nothing: the data
    // model draws its own path between the columns. After a Graphviz layout,
    // its curve is reused as is while it still joins the two columns.
    if (spline) {
        const std::optional<Route> layout = Link::route(board);
        const auto closeTo = [](QPointF p, QPointF q) { return std::hypot(p.x() - q.x(), p.y() - q.y()) <= 2; };
        if (layout && !layout->curve.empty() && closeTo(layout->a, ends->from.point) &&
            closeTo(layout->b, ends->to.point))
            return layout;
    }
    const QPointF a = ends->from.point;
    const QPointF b = ends->to.point;
    const qreal middleX = (a.x() + b.x()) / 2;
    Route r;
    r.a = a;
    r.b = b;
    // Orthogonal: two rows at different heights stay readable.
    if (std::abs(a.y() - b.y()) < 0.5)
        r.points = {a, b};
    else
        r.points = {a, QPointF(middleX, a.y()), QPointF(middleX, b.y()), b};
    r.labelPoint = polylineMidpoint(r.points);
    return r;
}

void RelationshipLink::draw(QPainter &painter, const Board &board, bool selected) const
{
    // A standard triangular head on the referenced column, as sidecar draws
    // it (the crow's feet were dropped there: the PK/FK badges and the
    // joined rows carry the meaning). Non-identifying relations are dashed,
    // as Mermaid draws them.
    const std::optional<Route> r = route(board);
    if (!r)
        return;
    const LinkStyle style = drawStyle(board, selected);
    QList<qreal> dash;
    if (!identifying)
        dash = {6, 4};
    strokeBody(painter, *r, {style.color, style.width, dash, arrowBaseInset(style.arrow), 0});
    fillArrowHead(painter, r->b, arrivalAngle(*r), style.arrow, style.color);
}

QList<Property> RelationshipLink::properties() const
{
    return {
        {QStringLiteral("fromCard"), tr("Cardinality at the start"), Property::Kind::Choice,
         withValue(fromCardChoices(), fromCard)},
        {QStringLiteral("toCard"), tr("Cardinality at the end"), Property::Kind::Choice,
         withValue(toCardChoices(), toCard)},
        {QStringLiteral("identifying"), tr("Identifying"), Property::Kind::Bool, {}},
    };
}

QVariant RelationshipLink::propertyValue(const QString &propertyId) const
{
    if (propertyId == QLatin1String("fromCard"))
        return fromCard;
    if (propertyId == QLatin1String("toCard"))
        return toCard;
    if (propertyId == QLatin1String("identifying"))
        return identifying;
    return {};
}

void RelationshipLink::setPropertyValue(const QString &propertyId, const QVariant &value)
{
    if (propertyId == QLatin1String("fromCard"))
        fromCard = value.toString();
    else if (propertyId == QLatin1String("toCard"))
        toCard = value.toString();
    else if (propertyId == QLatin1String("identifying"))
        identifying = value.toBool();
}

} // namespace diagram::datamodel
