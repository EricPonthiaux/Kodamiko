#pragma once

#include "viewers/diagram/Shape.h"

// Flowcharts, ported from sidecar's flow-diagram.js: one shape class for the
// Mermaid node variants (several variants share a drawn type: process,
// decision, terminator), the borderless text, and the flow link with its
// line styles and heads.
namespace diagram::flow {

// The drawn type of a Mermaid variant ("diamond" → "decision").
QString typeOf(const QString &variant);

class FlowShape : public Shape {
public:
    explicit FlowShape(const QString &type);

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    void measure(const Board &board) override;
    void resizeTo(qreal width, qreal height, const Board &board) override;
    void onDoubleClick(Board &board) override;
    QList<Port> ports(const Board &board) const override;
    QPointF borderPoint(QPointF toward) const override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;
    QStringList graphvizNodeAttributes(const Board &board,
                                       const QString &rankdir,
                                       qreal width,
                                       qreal height) const override;

    QString displayText() const override { return text; }

    QList<Property> properties() const override;
    QVariant propertyValue(const QString &id) const override;
    void setPropertyValue(const QString &id, const QVariant &value) override;

    QString text;
    QString fill = QStringLiteral("#ffffff");
    // The source variant ("stadium", "cylinder"…): it regenerates the exact
    // Mermaid delimiters.
    QString variant;

private:
    qreal sizeFactor() const;
    qreal maxMeasureWidth() const;
    qreal textPaddingX() const;
    qreal textEditPaddingX() const;
    qreal labelCenterY() const;
    QList<QPointF> points() const;
    QPainterPath outline() const;
    void drawDetails(QPainter &painter, const QColor &color, qreal width) const;
    QString graphvizGeometry() const;

    QStringList lines_;
    std::optional<qreal> textBoxWidth_;
};

// Borderless text (sidecar reuses the free board's text node).
class FlowTextShape : public Shape {
public:
    FlowTextShape();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    void measure(const Board &board) override;
    void resizeTo(qreal width, qreal height, const Board &board) override;
    void onDoubleClick(Board &board) override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;

    QString displayText() const override { return text; }

    QString text;

private:
    QRectF textRect() const;

    QStringList lines_;
    qreal minWidth_ = 60;
    qreal minHeight_ = 34;
};

class FlowLink : public Link {
public:
    FlowLink();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    QStringList graphvizAttributes(const Board &board) const override;
    LinkStyle drawStyle(const Board &board, bool selected) const override;
    void draw(QPainter &painter, const Board &board, bool selected) const override;
    QList<Property> properties() const override;
    QVariant propertyValue(const QString &id) const override;
    void setPropertyValue(const QString &id, const QVariant &value) override;

    QString line = QStringLiteral("solid"); // solid | thick | dotted | invisible
    QString head = QStringLiteral("arrow"); // arrow | none | circle | cross
};

} // namespace diagram::flow
