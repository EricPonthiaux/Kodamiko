#include "DataModel.h"

#include "References.h"
#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/families/Families.h"

#include <QCoreApplication>
#include <QPainter>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace diagram::datamodel {

using namespace detail;

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
    const QStringList words = raw.toLower().split(separators, Qt::SkipEmptyParts);
    const QString joined = normalizedIdentifier(raw);
    const QString last = words.isEmpty() ? joined : words.constLast();
    QSet<QString> result;
    for (const QString &candidate : {joined, last, singular(joined), singular(last)}) {
        if (!candidate.isEmpty())
            result.insert(candidate);
    }
    return result;
}

// How surely a FK column references a table known by `candidates`: its
// comment names the table, else its name is (or ends with) the table's
// name followed by `id`.
int foreignKeyScore(const Field &field, const QSet<QString> &candidates)
{
    const std::optional<Reference> reference = referencedColumn(field.comment);
    int score = reference && candidates.contains(normalizedIdentifier(reference->table)) ? 1000 : 0;
    const QString fieldName = normalizedIdentifier(field.name);
    for (const QString &candidate : candidates) {
        if (fieldName == candidate + QLatin1String("id"))
            score = std::max(score, 500);
        else if (fieldName.endsWith(candidate + QLatin1String("id")))
            score = std::max(score, 300);
    }
    return score;
}

// The end of a relation lying on `table`, null if the relation does not touch it.
const RelationshipLink::End *endOn(const RelationshipLink::ColumnEndpoints &ends, const EntityShape *table)
{
    if (ends.fromShape == table)
        return &ends.from;
    if (ends.toShape == table)
        return &ends.to;
    return nullptr;
}

// A table, not the server hosting it: outline, header row, then columns;
// the grid stays recognizable at the header's small size.
void drawTableIcon(QPainter &painter, qreal cx, qreal cy, const QColor &color)
{
    const qreal x = cx - 9;
    const qreal y = cy - 8;
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

} // namespace

namespace detail {

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

} // namespace detail

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
    if (!tokens.isEmpty())
        field.name = tokens.takeFirst();
    else
        field.name = field.type.isEmpty() ? QStringLiteral("column") : field.type;
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
    for (const QString &part :
         {field.type, field.name, field.constraints.join(QStringLiteral(", ")),
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
    name =
        jsonString(props, QStringLiteral("name"), jsonString(props, QStringLiteral("text"), QStringLiteral("TABLE")));
    const QStringList raw = props.contains(QStringLiteral("attributes"))
                                ? jsonStrings(props, QStringLiteral("attributes"))
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
        const int score = foreignKeyScore(field, candidates);
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
        // A name without letters or digits cannot be looked up again.
        const std::optional<QPointF> anchor = columnAnchor(field.name, true);
        if (!anchor)
            continue;
        result.append({x, anchor->y(), field.name});
        result.append({x + w, anchor->y(), field.name});
    }
    return result;
}

std::optional<Port> EntityShape::connectionPortAt(const Board &, QPointF point) const
{
    const QList<Field> &fields = databaseFields();
    if (fields.isEmpty())
        return std::nullopt;
    const qreal top = y + headerH + columnsH;
    const qsizetype index =
        std::clamp<qsizetype>(qsizetype(std::floor((point.y() - top) / rowH)), 0, fields.size() - 1);
    const Field &field = fields[index];
    const std::optional<QPointF> anchor = columnAnchor(field.name, point.x() < cx());
    if (!anchor)
        return std::nullopt;
    return Port{anchor->x(), anchor->y(), field.name};
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
        const RelationshipLink::End *end = endOn(*ends, this);
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
    const QPainterPath outline = roundRect(x, y, w, h, shapeRadiusSm);
    shadowFill(painter, outline, fillColor(palette.fill), style);
    stroke(painter, outline, strokeColor(state.selected ? style.shape.selectedStroke : palette.border),
           state.selected ? 2 : shapeBorderWidth);
    if (state.editing)
        return;

    painter.save();
    painter.setClipPath(outline, Qt::IntersectClip);
    drawTitleBar(painter, palette);
    drawColumnHeadings(painter, style);
    const QList<Field> &fields = databaseFields();
    if (fields.isEmpty())
        text::draw(painter, noneFont, tr("No columns"), QPointF(x + pad, y + headerH + columnsH + rowH / 2),
                   textColor(palette.inkMuted), Align::Left, Baseline::Middle);
    const QHash<QString, QSet<bool>> related = relatedColumns(board);
    for (qsizetype index = 0; index < fields.size(); ++index)
        drawRow(painter, style, index, related.value(fields[index].name));
    painter.restore();
}

void EntityShape::drawTitleBar(QPainter &painter, const Palette &palette) const
{
    painter.fillRect(QRectF(x, y, w, headerH), fillColor(palette.solid));
    const QColor headerInk = textColor(Qt::white);
    drawTableIcon(painter, x + 23, y + headerH / 2, headerInk);
    text::draw(painter, titleFont, text::elided(titleFont, name, w - 58), QPointF(x + 43, y + headerH / 2), headerInk,
               Align::Left, Baseline::Middle);
}

void EntityShape::drawColumnHeadings(QPainter &painter, const Style &style) const
{
    const QColor mutedInk = textColor(style.palette.inkMuted);
    const qreal nameX = x + pad;
    const qreal typeX = nameX + nameW_;
    const qreal keysX = typeX + typeW_;
    painter.fillRect(QRectF(x, y + headerH, w, columnsH), fillColor(style.tones.data));
    const qreal headerMiddle = y + headerH + columnsH / 2;
    text::draw(painter, headerFont, tr("COLUMN"), QPointF(nameX, headerMiddle), mutedInk, Align::Left,
               Baseline::Middle);
    text::draw(painter, headerFont, tr("TYPE"), QPointF(typeX, headerMiddle), mutedInk, Align::Left, Baseline::Middle);
    text::draw(painter, headerFont, tr("KEYS"), QPointF(keysX, headerMiddle), mutedInk, Align::Left, Baseline::Middle);
    for (const qreal sx : {typeX - 8, keysX - 8})
        strokeLine(painter, QPointF(sx, y + headerH), QPointF(sx, y + h), style.palette.separator, 1);
}

void EntityShape::drawRow(QPainter &painter, const Style &style, qsizetype index, const QSet<bool> &relatedSides) const
{
    const Palette &palette = style.palette;
    const QColor mutedInk = textColor(palette.inkMuted);
    const Field &field = databaseFields()[index];
    const qreal nameX = x + pad;
    const qreal typeX = nameX + nameW_;
    const qreal ry = y + headerH + columnsH + index * rowH;
    const qreal middle = ry + rowH / 2;
    if (index % 2)
        painter.fillRect(QRectF(x, ry, w, rowH), withAlpha(style.tones.sage, customFill.isEmpty() ? 0.45 : 0.16));
    if (!relatedSides.isEmpty())
        painter.fillRect(QRectF(x, ry, w, rowH), withAlpha(palette.selection, 0.08));
    strokeLine(painter, QPointF(x, ry), QPointF(x + w, ry), palette.separator, 1);

    text::draw(painter, columnFont, text::elided(columnFont, field.name, nameW_ - 24), QPointF(nameX, middle),
               field.has("PK") ? textColor(palette.ink) : mutedInk, Align::Left, Baseline::Middle);
    text::draw(painter, typeFont, text::elided(typeFont, field.type, typeW_ - 22), QPointF(typeX, middle), mutedInk,
               Align::Left, Baseline::Middle);
    drawKeyBadges(painter, palette, field, QPointF(typeX + typeW_, ry));
    for (const bool left : relatedSides) {
        const QPainterPath dot = ellipse(QPointF(left ? x + 3 : x + w - 3, middle), 3, 3);
        painter.fillPath(dot, style.link.color);
    }
}

void EntityShape::drawKeyBadges(QPainter &painter, const Palette &palette, const Field &field, QPointF rowTopLeft) const
{
    const qreal middle = rowTopLeft.y() + rowH / 2;
    qreal badgeX = rowTopLeft.x();
    for (const QString &constraint : field.constraints) {
        const bool foreign = constraint == QLatin1String("FK");
        const QColor tone = foreign ? palette.line : palette.selection;
        const QPainterPath badge = roundRect(badgeX, rowTopLeft.y() + 8, badgeW, 15, 4);
        painter.fillPath(badge, withAlpha(tone, constraint == QLatin1String("PK") ? 0.18 : 0.1));
        stroke(painter, badge, tone, 1);
        text::draw(painter, badgeFont, constraint, QPointF(badgeX + badgeW / 2, middle),
                   customFill.isEmpty() ? tone : textColor(palette.ink), Align::Center, Baseline::Middle);
        badgeX += badgeW + 4;
    }
}

QList<Property> EntityShape::properties() const
{
    return {{QStringLiteral("name"), tr("Table"), Property::Kind::Text, {}},
            {QStringLiteral("attributes"), tr("Columns (type name keys \"comment\")"), Property::Kind::Lines, {}}};
}

QVariant EntityShape::propertyValue(const QString &propertyId) const
{
    if (propertyId == QLatin1String("name"))
        return name;
    if (propertyId == QLatin1String("attributes"))
        return attributes.join(QLatin1Char('\n'));
    return {};
}

void EntityShape::setPropertyValue(const QString &propertyId, const QVariant &value)
{
    if (propertyId == QLatin1String("name")) {
        const QString text = value.toString().trimmed();
        name = text.isEmpty() ? QStringLiteral("TABLE") : text;
    } else if (propertyId == QLatin1String("attributes")) {
        // Each keystroke sends the whole text again: normalizing it as it
        // comes loses nothing of what is being typed.
        attributes.clear();
        for (const QString &line : value.toString().split(QLatin1Char('\n'))) {
            if (!line.trimmed().isEmpty())
                attributes.append(databaseFieldText(line));
        }
    }
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
        f.links = {
            {QStringLiteral("relationship"), [] { return std::unique_ptr<Link>(new RelationshipLink); }, false, true}};
        f.defaultShape = QStringLiteral("entity");
        f.defaultLink = QStringLiteral("relationship");
        f.cardinalities = true;
        return f;
    }();
    return family;
}

} // namespace diagram::families
