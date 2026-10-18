#pragma once

#include "viewers/diagram/Shape.h"

#include <QHash>
#include <QSet>

// Data models, ported from sidecar's erd-diagram.js: a physical schema whose
// tables list their columns (type, name, PK/FK/UK keys, comment) and whose
// relations join a foreign key column to the column it references.
namespace diagram::datamodel {

// A column, as written in a Mermaid erDiagram block: `type name KEYS "comment"`.
struct Field {
    QString raw;
    QString type;
    QString name;
    QStringList constraints; // among PK, FK, UK, in that order
    QString comment;

    bool has(const char *constraint) const { return constraints.contains(QLatin1String(constraint)); }
};

Field parseDatabaseField(const QString &value);
// The canonical text of a column (what is edited and serialized).
QString databaseFieldText(const Field &field);
QString databaseFieldText(const QString &value);

class EntityShape : public Shape {
public:
    EntityShape();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    void measure(const Board &board) override;
    void onDoubleClick(Board &board) override;
    QList<Port> connectionPorts(const Board &board) const override;
    std::optional<Port> connectionPortAt(const Board &board, QPointF point) const override;
    QPointF connectionPoint(const Board &board, QPointF from, QPointF pointer) const override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;
    QStringList graphvizNodeAttributes(const Board &board,
                                       const QString &rankdir,
                                       qreal width,
                                       qreal height) const override;

    QString displayText() const override { return name; }

    QList<Property> properties() const override;
    QVariant propertyValue(const QString &propertyId) const override;
    void setPropertyValue(const QString &propertyId, const QVariant &value) override;

    // The columns, parsed from `attributes` (kept in sync lazily).
    const QList<Field> &databaseFields() const;
    // A column by name, case and punctuation aside; null if none.
    const Field *column(const QString &name) const;
    // The PK, else a column named id, else the first one.
    const Field *primaryColumn() const;

    struct ForeignKey {
        const Field *field = nullptr;
        int score = 0;
    };

    // The FK column of this table that most likely references `table`: by
    // its comment (`references customer.id`), then by its name
    // (`customer_id`); a single FK is taken even without a clue.
    std::optional<ForeignKey> foreignKeyTo(const EntityShape &table) const;
    // Where a link meets a column's row, on the left or right edge.
    std::optional<QPointF> columnAnchor(const QString &column, bool left) const;
    // The record port of a column in the Graphviz node ("column_2").
    QString graphvizPortForColumn(const QString &column) const;
    // The columns joined by a relationship, with the sides they are joined on.
    QHash<QString, QSet<bool>> relatedColumns(const Board &board) const;

    QString name = QStringLiteral("TABLE");
    // The columns as text: the editable and serializable form. Parsing them
    // when measured and drawn keeps old boards and Mermaid sources as is.
    QStringList attributes;

private:
    void drawTitleBar(QPainter &painter, const Palette &palette) const;
    void drawColumnHeadings(QPainter &painter, const Style &style) const;
    // One column's row; `relatedSides` are the edges where relations join it.
    void drawRow(QPainter &painter, const Style &style, qsizetype index, const QSet<bool> &relatedSides) const;
    void drawKeyBadges(QPainter &painter, const Palette &palette, const Field &field, QPointF rowTopLeft) const;

    mutable QList<Field> fields_;
    mutable QStringList parsedFrom_;
    qreal nameW_ = 130;
    qreal typeW_ = 100;
};

class RelationshipLink : public Link {
public:
    RelationshipLink();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    std::optional<Route> route(const Board &board) const override;
    void draw(QPainter &painter, const Board &board, bool selected) const override;

    // In a physical schema the line already joins the visible FK and target
    // columns: the Mermaid verb stays in the source, but is neither drawn
    // nor given room by Graphviz.
    void drawLabel(QPainter &, const Board &, bool) const override { }

    QString layoutLabel(const Board &) const override { return {}; }

    std::optional<std::pair<GraphvizEndpoint, GraphvizEndpoint>> graphvizEndpoints(
        const Board &board,
        const QString &rankdir) const override;
    QList<Property> properties() const override;
    QVariant propertyValue(const QString &propertyId) const override;
    void setPropertyValue(const QString &propertyId, const QVariant &value) override;

    // The tables and columns the relation joins: the FK side is always the
    // source (it may be the link's `to`), the referenced column the target.
    struct Endpoints {
        const EntityShape *fromShape = nullptr;
        const EntityShape *toShape = nullptr;
        QString fromColumn;
        QString toColumn;
    };

    Endpoints relationshipEndpoints(const EntityShape &fromShape, const EntityShape &toShape) const;

    struct End {
        QString column;
        bool left = false;
        QPointF point;
    };

    struct ColumnEndpoints {
        const EntityShape *fromShape = nullptr;
        const EntityShape *toShape = nullptr;
        End from;
        End to;
    };

    std::optional<ColumnEndpoints> columnEndpoints(const Board &board) const;

    QString fromCard = QStringLiteral("||");
    QString toCard = QStringLiteral("o{");
    // Columns chosen by hand (dragged from a column's port).
    QString fromColumn;
    QString toColumn;
    // Mermaid's `--` (identifying) or `..` (non-identifying, dashed).
    bool identifying = true;

private:
    // The endpoints when no column chosen by hand is a FK: the tables' own
    // FKs decide, else their primary columns are joined.
    Endpoints foreignKeyEndpoints(const EntityShape &fromShape,
                                  const EntityShape &toShape,
                                  const Field *selectedFrom,
                                  const Field *selectedTo) const;
};

} // namespace diagram::datamodel
