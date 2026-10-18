#pragma once

#include "viewers/diagram/Shape.h"

// C4 architecture diagrams, ported from sidecar's c4-diagram.js. The
// notation stays deliberately self-describing:
// - people use a recognisable person silhouette;
// - systems, containers and components have distinct visual weights;
// - databases and queues have their conventional architecture silhouettes;
// - external dependencies are grey and dashed;
// - dashed boundaries make scope and ownership visible.
namespace diagram::c4 {

// The element type written in brackets under its name: "[Software system ·
// Database]".
QString kindTerm(const QString &kind);

class C4ElementShape : public Shape {
public:
    C4ElementShape();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    void measure(const Board &board) override;
    void resizeTo(qreal width, qreal height, const Board &board) override;
    void onDoubleClick(Board &board) override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;

    QString displayText() const override { return name; }

    QList<Property> properties() const override;
    QVariant propertyValue(const QString &propertyId) const override;
    void setPropertyValue(const QString &propertyId, const QVariant &value) override;

    QString name;
    QString kind = QStringLiteral("system"); // person | system | container | component
    QString description;
    QString technology;
    bool external = false;
    QString variant = QStringLiteral("box"); // box | database | queue

    // The lines laid out by the last measure (tests check they fit).
    QStringList titleLines() const { return titleLines_; }

    QStringList typeLines() const { return typeLines_; }

    QStringList technologyLines() const { return technologyLines_; }

    QStringList descriptionLines() const { return descriptionLines_; }

private:
    struct BodyStyle {
        QColor fill, stroke, ink, muted;
    };

    BodyStyle bodyStyle(const Style &style) const;
    QString typeText() const;
    qreal contentTop() const;
    // Wraps the texts at the current width; returns the height they need.
    qreal layoutText();
    QPainterPath boxPath() const;
    void drawPerson(QPainter &painter, const QColor &ink) const;
    // The database cap or the queue's end lines, over the box.
    void drawVariantMarks(QPainter &painter, const QColor &color) const;

    // Where the texts line up: centered under a person or a database, flush
    // left otherwise.
    struct TextColumn {
        qreal x = 0;
        Align align = Align::Left;
    };

    TextColumn textColumn() const;
    void drawTexts(QPainter &painter, const Style &style, const BodyStyle &body) const;
    // Fills the pill behind the technology lines starting at `top`; returns
    // its height.
    qreal drawTechnologyPill(QPainter &painter, const Style &style, const TextColumn &column, qreal top) const;

    QStringList titleLines_;
    QStringList typeLines_;
    QStringList technologyLines_;
    QStringList descriptionLines_;
};

// A boundary: a Graphviz cluster holding the elements whose parentBoundary
// names it.
class C4BoundaryShape : public Shape {
public:
    C4BoundaryShape();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    void measure(const Board &board) override;
    bool hitTest(QPointF point) const override;
    void onDoubleClick(Board &board) override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;

    bool drawBehindLinks() const override { return true; }

    // Sidecar recognises its boundaries by type, both as Graphviz clusters
    // and as the frames a created element joins.
    bool isGraphvizCluster() const override { return true; }

    bool isCreationContainer() const override { return true; }

    QString layoutLabel() const override;
    qreal layoutLabelFontPx() const override;
    std::optional<QSizeF> layoutMinimumSize() const override;

    QString displayText() const override { return name; }

    QList<Property> properties() const override;
    QVariant propertyValue(const QString &propertyId) const override;
    void setPropertyValue(const QString &propertyId, const QVariant &value) override;

    QString name;
    // Enterprise_Boundary | System_Boundary | Container_Boundary |
    // Deployment_Node | Boundary
    QString boundaryKind = QStringLiteral("System_Boundary");

private:
    QString kindLabel() const;
    QString title() const;

    qreal labelWidth_ = 0;
};

class C4RelationLink : public Link {
public:
    C4RelationLink();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    QStringList graphvizAttributes(const Board &board) const override;
    void draw(QPainter &painter, const Board &board, bool selected) const override;
    LabelBox labelBox(const Board &board) const override;
    QString layoutLabel(const Board &board) const override;
    QString graphvizHtmlLabel(const Board &board) const override;
    QList<Property> properties() const override;
    QVariant propertyValue(const QString &propertyId) const override;
    void setPropertyValue(const QString &propertyId, const QVariant &value) override;

    // The label and its [technology], wrapped in the font the panel is
    // painted with.
    QStringList displayLines(const Board &board) const;

    QString technology;
    QString description;
    bool bidirectional = false;
};

} // namespace diagram::c4
