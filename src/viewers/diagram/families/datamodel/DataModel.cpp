#include "DataModel.h"

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/families/Families.h"

#include <QCoreApplication>
#include <QPainter>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace diagram::datamodel {

namespace {

// sidecar's fontMono is its sans font: every text here is Inter.
const Font titleFont(15, 600);
const Font headerFont(10, 600);
const Font columnFont(13);
const Font typeFont(11.5);
const Font badgeFont(9, 700);
const Font noneFont(11.5, 400, true);
const Font editFont(12);
constexpr qreal headerH = 48;
constexpr qreal columnsH = 25;
constexpr qreal rowH = 31;
constexpr qreal pad = 16;
constexpr qreal badgeW = 23;
const QStringList keyOrder{QStringLiteral("PK"), QStringLiteral("FK"), QStringLiteral("UK")};

QString tr(const char *text)
{
    return QCoreApplication::translate("DataModelDiagram", text);
}

QString normalizedIdentifier(const QString &value)
{
    static const QRegularExpression other(QStringLiteral("[^a-z0-9]"));
    return value.toLower().remove(other);
}

QString singular(const QString &value)
{
    if (value.endsWith(QLatin1String("ies")) && value.size() > 3)
        return value.chopped(3) + QLatin1Char('y');
    if (value.endsWith(QLatin1Char('s')) && !value.endsWith(QLatin1String("ss")) && value.size() > 1)
        return value.chopped(1);
    return value;
}

// The names a foreign key may give its table: `order_items`, `items`,
// `order_item`, `item`.
QSet<QString> tableIdentifiers(const EntityShape &table)
{
    const QString raw = table.name.split(QLatin1Char('.')).constLast();
    static const QRegularExpression separators(QStringLiteral("[^a-z0-9]+"));
    QStringList words = raw.toLower().split(separators, Qt::SkipEmptyParts);
    const QString joined = normalizedIdentifier(raw);
    const QString last = words.isEmpty() ? joined : words.constLast();
    QSet<QString> result;
    for (const QString &candidate : {joined, last, singular(joined), singular(last)}) {
        if (!candidate.isEmpty())
            result.insert(candidate);
    }
    return result;
}

struct Reference {
    QString table;
    QString column;
};

// A comment naming the referenced column: `references customer.id`,
// `ref customer.id`, `-> customer.id` or just `customer.id`.
std::optional<Reference> referencedColumn(const QString &comment)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(?:(?:references?|ref(?:erences)?)\\s+|->\\s*)?([\\w.-]+)\\.([\\w.-]+)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = pattern.match(comment.trimmed());
    if (!m.hasMatch())
        return std::nullopt;
    return Reference{m.captured(1), m.captured(2)};
}

// A table, not the server hosting it: outline, header row, then columns;
// the grid stays recognizable at the header's small size.
void drawTableIcon(QPainter &painter, qreal cx, qreal cy, const QColor &color)
{
    const qreal x = cx - 9, y = cy - 8;
    stroke(painter, roundRect(x, y, 18, 16, 1.75), color, 1.4);
    QPainterPath grid;
    grid.moveTo(x, y + 5.5);
    grid.lineTo(x + 18, y + 5.5);
    grid.moveTo(x + 6, y + 5.5);
    grid.lineTo(x + 6, y + 16);
    grid.moveTo(x + 12, y + 5.5);
    grid.lineTo(x + 12, y + 16);
    stroke(painter, grid, color, 1.4);
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
// Fields

Field parseDatabaseField(const QString &value)
{
    Field field;
    field.raw = value.trimmed();
    QString statement = field.raw;
    static const QRegularExpression quoted(QStringLiteral("\\s+[\"']([^\"']*)[\"']\\s*$"));
    const QRegularExpressionMatch m = quoted.match(statement);
    if (m.hasMatch()) {
        field.comment = m.captured(1);
        statement = statement.left(m.capturedStart()).trimmed();
    }
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    QStringList tokens = statement.split(spaces, Qt::SkipEmptyParts);
    field.type = tokens.isEmpty() ? QString() : tokens.takeFirst();
    field.name = !tokens.isEmpty() ? tokens.takeFirst() : !field.type.isEmpty() ? field.type : QStringLiteral("column");
    for (const QString &token : tokens) {
        for (const QString &part : token.split(QLatin1Char(','))) {
            const QString key = part.trimmed().toUpper();
            if (keyOrder.contains(key) && !field.constraints.contains(key))
                field.constraints.append(key);
        }
    }
    std::sort(field.constraints.begin(), field.constraints.end(),
              [](const QString &a, const QString &b) { return keyOrder.indexOf(a) < keyOrder.indexOf(b); });
    return field;
}

QString databaseFieldText(const Field &field)
{
    // Mermaid separates several keys with commas (`PK, FK`).
    QStringList parts;
    for (const QString &part : {field.type, field.name, field.constraints.join(QStringLiteral(", ")),
                                field.comment.isEmpty() ? QString() : QLatin1Char('"') + field.comment + QLatin1Char('"')}) {
        if (!part.isEmpty())
            parts.append(part);
    }
    return parts.join(QLatin1Char(' '));
}

QString databaseFieldText(const QString &value)
{
    return databaseFieldText(parseDatabaseField(value));
}

// ---------------------------------------------------------------------------
// EntityShape

EntityShape::EntityShape()
    : Shape(QStringLiteral("entity"), 330, 150)
{
}

void EntityShape::load(const QJsonObject &props)
{
    Shape::load(props);
    name = jsonString(props, QStringLiteral("name"), jsonString(props, QStringLiteral("text"), QStringLiteral("TABLE")));
    const QStringList raw = props.contains(QStringLiteral("attributes")) ? jsonStrings(props, QStringLiteral("attributes"))
                                                                         : jsonStrings(props, QStringLiteral("fields"));
    attributes.clear();
    for (const QString &line : raw)
        attributes.append(databaseFieldText(line));
}

QJsonObject EntityShape::toJson() const
{
    QJsonObject json = Shape::toJson();
    json.insert(QStringLiteral("name"), name);
    json.insert(QStringLiteral("attributes"), toJsonArray(attributes));
    json.insert(QStringLiteral("fields"), toJsonArray(attributes));
    return json;
}

const QList<Field> &EntityShape::databaseFields() const
{
    if (parsedFrom_ != attributes || fields_.size() != attributes.size()) {
        fields_.clear();
        for (const QString &line : attributes)
            fields_.append(parseDatabaseField(line));
        parsedFrom_ = attributes;
    }
    return fields_;
}

const Field *EntityShape::column(const QString &columnName) const
{
    const QString normalized = normalizedIdentifier(columnName);
    if (normalized.isEmpty())
        return nullptr;
    for (const Field &field : databaseFields()) {
        if (normalizedIdentifier(field.name) == normalized)
            return &field;
    }
    return nullptr;
}

const Field *EntityShape::primaryColumn() const
{
    const QList<Field> &fields = databaseFields();
    for (const Field &field : fields) {
        if (field.has("PK"))
            return &field;
    }
    for (const Field &field : fields) {
        if (normalizedIdentifier(field.name) == QLatin1String("id"))
            return &field;
    }
    return fields.isEmpty() ? nullptr : &fields.front();
}

std::optional<EntityShape::ForeignKey> EntityShape::foreignKeyTo(const EntityShape &table) const
{
    const QSet<QString> candidates = tableIdentifiers(table);
    std::optional<ForeignKey> best;
    int foreignKeys = 0;
    for (const Field &field : databaseFields()) {
        if (!field.has("FK"))
            continue;
        ++foreignKeys;
        const std::optional<Reference> reference = referencedColumn(field.comment);
        int score = reference && candidates.contains(normalizedIdentifier(reference->table)) ? 1000 : 0;
        const QString fieldName = normalizedIdentifier(field.name);
        for (const QString &candidate : candidates) {
            if (fieldName == candidate + QLatin1String("id"))
                score = std::max(score, 500);
            else if (fieldName.endsWith(candidate + QLatin1String("id")))
                score = std::max(score, 300);
        }
        if (!best || score > best->score)
            best = ForeignKey{&field, score};
    }
    if ((best && best->score) || foreignKeys == 1)
        return best;
    return std::nullopt;
}

std::optional<QPointF> EntityShape::columnAnchor(const QString &columnName, bool left) const
{
    const Field *field = column(columnName);
    if (!field)
        return std::nullopt;
    const qsizetype index = field - databaseFields().constData();
    return QPointF(left ? x : x + w, y + headerH + columnsH + index * rowH + rowH / 2);
}

QString EntityShape::graphvizPortForColumn(const QString &columnName) const
{
    const Field *field = column(columnName);
    if (!field)
        return {};
    return QStringLiteral("column_%1").arg(field - databaseFields().constData());
}

void EntityShape::measure(const Board &)
{
    const QList<Field> &fields = databaseFields();
    const qreal titleW = text::width(titleFont, name) + 58;
    nameW_ = 125;
    typeW_ = 95;
    bool severalKeys = false;
    for (const Field &field : fields) {
        nameW_ = std::max(nameW_, text::width(columnFont, field.name) + 24);
        typeW_ = std::max(typeW_, text::width(typeFont, field.type) + 22);
        severalKeys = severalKeys || field.constraints.size() > 1;
    }
    const qreal keysW = severalKeys ? 92 : 66;
    w = std::ceil(std::max({qreal(310), titleW, pad * 2 + nameW_ + typeW_ + keysW}));
    h = headerH + columnsH + std::max<qsizetype>(1, fields.size()) * rowH + 1;
}

QList<Port> EntityShape::connectionPorts(const Board &board) const
{
    const QList<Field> &fields = databaseFields();
    if (fields.isEmpty())
        return Shape::connectionPorts(board);
    QList<Port> result;
    for (const Field &field : fields) {
        const qreal py = columnAnchor(field.name, true)->y();
        result.append({x, py, field.name});
        result.append({x + w, py, field.name});
    }
    return result;
}

std::optional<Port> EntityShape::connectionPortAt(const Board &, QPointF point) const
{
    const QList<Field> &fields = databaseFields();
    if (fields.isEmpty())
        return std::nullopt;
    const qreal top = y + headerH + columnsH;
    const qsizetype index = std::clamp<qsizetype>(qsizetype(std::floor((point.y() - top) / rowH)), 0, fields.size() - 1);
    const Field &field = fields[index];
    const QPointF anchor = *columnAnchor(field.name, point.x() < cx());
    return Port{anchor.x(), anchor.y(), field.name};
}

QPointF EntityShape::connectionPoint(const Board &board, QPointF from, QPointF pointer) const
{
    if (const std::optional<Port> port = connectionPortAt(board, pointer))
        return {port->x, port->y};
    return Shape::connectionPoint(board, from, pointer);
}

QStringList EntityShape::graphvizNodeAttributes(const Board &, const QString &, qreal width, qreal height) const
{
    // An HTML table of the exact size, one cell per column row: Graphviz
    // then attaches each relation to the row of its column.
    const QList<Field> &fields = databaseFields();
    const int tableWidth = std::max(1, int(std::ceil(width)));
    const int tableHeight = std::max(1, int(std::ceil(height)));
    const auto row = [&](qreal rowHeight, const QString &port) {
        return QStringLiteral("<TR><TD %1FIXEDSIZE=\"TRUE\" WIDTH=\"%2\" HEIGHT=\"%3\"></TD></TR>")
            .arg(port.isEmpty() ? QString() : QStringLiteral("PORT=\"%1\" ").arg(port))
            .arg(tableWidth)
            .arg(rowHeight);
    };
    QString rows = row(headerH + columnsH, {});
    if (fields.isEmpty()) {
        rows += row(rowH, {});
    } else {
        for (qsizetype i = 0; i < fields.size(); ++i)
            rows += row(rowH, QStringLiteral("column_%1").arg(i));
    }
    const QString label = QStringLiteral("<TABLE BORDER=\"0\" CELLBORDER=\"0\" CELLSPACING=\"0\" CELLPADDING=\"0\" "
                                         "FIXEDSIZE=\"TRUE\" WIDTH=\"%1\" HEIGHT=\"%2\">%3</TABLE>")
                              .arg(tableWidth)
                              .arg(tableHeight)
                              .arg(rows);
    return {QStringLiteral("shape=plain"), QStringLiteral("fixedsize=false"), QStringLiteral("margin=0"),
            QStringLiteral("label=<") + label + QLatin1Char('>')};
}

QHash<QString, QSet<bool>> EntityShape::relatedColumns(const Board &board) const
{
    QHash<QString, QSet<bool>> result;
    for (const auto &l : board.links()) {
        const auto *relation = dynamic_cast<const RelationshipLink *>(l.get());
        if (!relation)
            continue;
        const std::optional<RelationshipLink::ColumnEndpoints> ends = relation->columnEndpoints(board);
        if (!ends)
            continue;
        const RelationshipLink::End *end = ends->fromShape == this ? &ends->from
                                           : ends->toShape == this ? &ends->to
                                                                   : nullptr;
        if (end && !end->column.isEmpty())
            result[end->column].insert(end->left);
    }
    return result;
}

void EntityShape::onDoubleClick(Board &board)
{
    // The name, a `--` line, then a column per line.
    TextEditSpec spec;
    spec.get = [this] { return (QStringList{name, QStringLiteral("--")} + attributes).join(QLatin1Char('\n')); };
    spec.set = [this](const QString &value) {
        QStringList lines;
        for (const QString &line : value.split(QLatin1Char('\n'))) {
            if (!line.trimmed().isEmpty())
                lines.append(line.trimmed());
        }
        const qsizetype separator = lines.indexOf(QStringLiteral("--"));
        name = !lines.isEmpty() && lines.front() != QLatin1String("--") ? lines.front() : QStringLiteral("TABLE");
        attributes.clear();
        for (const QString &line : lines.mid(separator >= 0 ? separator + 1 : 1))
            attributes.append(databaseFieldText(line));
    };
    spec.color = board.style().palette.ink;
    spec.font = editFont;
    spec.lineHeight = 19;
    spec.rect = [this] { return QRectF(x + pad, y + pad, w - pad * 2, h - pad * 2); };
    spec.enterCommits = false;
    board.beginTextEdit(this, spec);
}

void EntityShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const Palette &palette = style.palette;
    const QColor bodyInk = textColor(palette.ink);
    const QColor mutedInk = textColor(palette.inkMuted);
    const QPainterPath outline = roundRect(x, y, w, h, ShapeRadiusSm);
    shadowFill(painter, outline, fillColor(palette.fill), style);
    stroke(painter, outline, strokeColor(state.selected ? style.shape.selectedStroke : palette.border),
           state.selected ? 2 : ShapeBorderWidth);
    if (state.editing)
        return;

    painter.save();
    painter.setClipPath(outline, Qt::IntersectClip);

    painter.fillRect(QRectF(x, y, w, headerH), fillColor(palette.solid));
    const QColor headerInk = textColor(Qt::white);
    drawTableIcon(painter, x + 23, y + headerH / 2, headerInk);
    text::draw(painter, titleFont, text::elided(titleFont, name, w - 58), QPointF(x + 43, y + headerH / 2), headerInk,
               Align::Left, Baseline::Middle);

    const QList<Field> &fields = databaseFields();
    const qreal nameX = x + pad;
    const qreal typeX = nameX + nameW_;
    const qreal keysX = typeX + typeW_;
    painter.fillRect(QRectF(x, y + headerH, w, columnsH), fillColor(style.tones.data));
    const qreal headerMiddle = y + headerH + columnsH / 2;
    text::draw(painter, headerFont, tr("COLUMN"), QPointF(nameX, headerMiddle), mutedInk, Align::Left, Baseline::Middle);
    text::draw(painter, headerFont, tr("TYPE"), QPointF(typeX, headerMiddle), mutedInk, Align::Left, Baseline::Middle);
    text::draw(painter, headerFont, tr("KEYS"), QPointF(keysX, headerMiddle), mutedInk, Align::Left, Baseline::Middle);
    for (const qreal sx : {typeX - 8, keysX - 8})
        strokeLine(painter, QPointF(sx, y + headerH), QPointF(sx, y + h), palette.separator, 1);

    if (fields.isEmpty())
        text::draw(painter, noneFont, tr("No columns"), QPointF(nameX, y + headerH + columnsH + rowH / 2), mutedInk,
                   Align::Left, Baseline::Middle);
    const QHash<QString, QSet<bool>> related = relatedColumns(board);
    for (qsizetype index = 0; index < fields.size(); ++index) {
        const Field &field = fields[index];
        const qreal ry = y + headerH + columnsH + index * rowH;
        const qreal middle = ry + rowH / 2;
        if (index % 2)
            painter.fillRect(QRectF(x, ry, w, rowH), withAlpha(style.tones.sage, customFill.isEmpty() ? 0.45 : 0.16));
        if (related.contains(field.name))
            painter.fillRect(QRectF(x, ry, w, rowH), withAlpha(palette.selection, 0.08));
        strokeLine(painter, QPointF(x, ry), QPointF(x + w, ry), palette.separator, 1);

        text::draw(painter, columnFont, text::elided(columnFont, field.name, nameW_ - 24), QPointF(nameX, middle),
                   field.has("PK") ? bodyInk : mutedInk, Align::Left, Baseline::Middle);
        text::draw(painter, typeFont, text::elided(typeFont, field.type, typeW_ - 22), QPointF(typeX, middle), mutedInk,
                   Align::Left, Baseline::Middle);

        qreal badgeX = keysX;
        for (const QString &constraint : field.constraints) {
            const bool foreign = constraint == QLatin1String("FK");
            const QColor tone = foreign ? palette.line : palette.selection;
            const QPainterPath badge = roundRect(badgeX, ry + 8, badgeW, 15, 4);
            painter.fillPath(badge, withAlpha(tone, constraint == QLatin1String("PK") ? 0.18 : 0.1));
            stroke(painter, badge, tone, 1);
            text::draw(painter, badgeFont, constraint, QPointF(badgeX + badgeW / 2, middle),
                       customFill.isEmpty() ? tone : bodyInk, Align::Center, Baseline::Middle);
            badgeX += badgeW + 4;
        }
        for (const bool left : related.value(field.name)) {
            const QPainterPath dot = ellipse(QPointF(left ? x + 3 : x + w - 3, middle), 3, 3);
            painter.fillPath(dot, style.link.color);
        }
    }
    painter.restore();
}

QList<Property> EntityShape::properties() const
{
    return {{QStringLiteral("name"), tr("Table"), Property::Kind::Text, {}},
            {QStringLiteral("attributes"), tr("Columns (type name keys \"comment\")"), Property::Kind::Lines, {}}};
}

QVariant EntityShape::propertyValue(const QString &id) const
{
    if (id == QLatin1String("name"))
        return name;
    if (id == QLatin1String("attributes"))
        return attributes.join(QLatin1Char('\n'));
    return {};
}

void EntityShape::setPropertyValue(const QString &id, const QVariant &value)
{
    if (id == QLatin1String("name")) {
        const QString text = value.toString().trimmed();
        name = text.isEmpty() ? QStringLiteral("TABLE") : text;
    } else if (id == QLatin1String("attributes")) {
        // Each keystroke sends the whole text again: normalizing it as it
        // comes loses nothing of what is being typed.
        attributes.clear();
        for (const QString &line : value.toString().split(QLatin1Char('\n'))) {
            if (!line.trimmed().isEmpty())
                attributes.append(databaseFieldText(line));
        }
    }
}

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
    const std::optional<EntityShape::ForeignKey> fromForeignKey = fromShape.foreignKeyTo(toShape);
    const std::optional<EntityShape::ForeignKey> toForeignKey = toShape.foreignKeyTo(fromShape);

    const auto endpoint = [](const EntityShape &owner, const EntityShape &referenced, const Field &foreignKey,
                             const Field *target) {
        if (!target) {
            if (const std::optional<Reference> reference = referencedColumn(foreignKey.comment))
                target = referenced.column(reference->column);
        }
        if (!target)
            target = referenced.primaryColumn();
        return Endpoints{&owner, &referenced, foreignKey.name, target ? target->name : QString()};
    };

    // Columns chosen by hand: their FK decides the direction, the FK being
    // always the source of the arrow.
    if (selectedFrom && selectedFrom->has("FK"))
        return endpoint(fromShape, toShape, *selectedFrom, selectedTo);
    if (selectedTo && selectedTo->has("FK"))
        return endpoint(toShape, fromShape, *selectedTo, selectedFrom);

    // An explicit target column goes with the FK found in the other table
    // (a natural key or a UK, not only `id`).
    if (selectedFrom && toForeignKey)
        return endpoint(toShape, fromShape, *toForeignKey->field, selectedFrom);
    if (selectedTo && fromForeignKey)
        return endpoint(fromShape, toShape, *fromForeignKey->field, selectedTo);

    const bool useFromForeignKey =
        fromForeignKey
        && (!toForeignKey || fromForeignKey->score > toForeignKey->score || fromCard.contains(QLatin1Char('}')));
    if (useFromForeignKey)
        return endpoint(fromShape, toShape, *fromForeignKey->field, nullptr);
    if (toForeignKey)
        return endpoint(toShape, fromShape, *toForeignKey->field, nullptr);

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
    return ColumnEndpoints{relationship.fromShape, relationship.toShape,
                           {relationship.fromColumn, !leftToRight, *a},
                           {relationship.toColumn, leftToRight, *b}};
}

std::optional<std::pair<GraphvizEndpoint, GraphvizEndpoint>>
RelationshipLink::graphvizEndpoints(const Board &board, const QString &rankdir) const
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
    return std::pair{GraphvizEndpoint{relationship.fromShape->id, fromPort, reverse ? QStringLiteral("w") : QStringLiteral("e")},
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
        if (layout && !layout->curve.empty() && closeTo(layout->a, ends->from.point) && closeTo(layout->b, ends->to.point))
            return layout;
    }
    const QPointF a = ends->from.point, b = ends->to.point;
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

QVariant RelationshipLink::propertyValue(const QString &id) const
{
    if (id == QLatin1String("fromCard"))
        return fromCard;
    if (id == QLatin1String("toCard"))
        return toCard;
    if (id == QLatin1String("identifying"))
        return identifying;
    return {};
}

void RelationshipLink::setPropertyValue(const QString &id, const QVariant &value)
{
    if (id == QLatin1String("fromCard"))
        fromCard = value.toString();
    else if (id == QLatin1String("toCard"))
        toCard = value.toString();
    else if (id == QLatin1String("identifying"))
        identifying = value.toBool();
}

} // namespace diagram::datamodel

// ---------------------------------------------------------------------------
// The family

namespace diagram::families {

const Family &dataModel()
{
    using namespace datamodel;
    static const Family family = [] {
        Family f;
        f.id = QStringLiteral("datamodel");
        f.title = tr("Data model");
        f.shapes = {{QStringLiteral("entity"), [] { return std::unique_ptr<Shape>(new EntityShape); }}};
        f.links = {{QStringLiteral("relationship"), [] { return std::unique_ptr<Link>(new RelationshipLink); }, false,
                    true}};
        f.defaultShape = QStringLiteral("entity");
        f.defaultLink = QStringLiteral("relationship");
        f.cardinalities = true;
        return f;
    }();
    return family;
}

} // namespace diagram::families
