#pragma once

#include "viewers/diagram/Shape.h"

// Explainer boards, ported from sidecar's free-board.js: the deliberately
// small visual vocabulary of a free explanation. The source format knows four
// public primitives: node, frame, edge and stroke. Hidden anchors let an edge
// end at a free point without teaching the engine a second link model.
namespace diagram::explainer {

// The values the scene contract accepts (sciilo-board-v1.schema.json).
const QStringList &shapeNames();
const QStringList &toneNames();
const QStringList &arrowNames();
const QStringList &lineNames();

// A box, a circle, a triangle… or a borderless text: one class draws them
// all, the geometry being a property the user changes.
class FreeNodeShape : public Shape {
public:
    FreeNodeShape();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    void measure(const Board &board) override;
    void resizeTo(qreal width, qreal height, const Board &board) override;
    bool staysInsideParent() const override { return true; }
    void onDoubleClick(Board &board) override;
    QPointF borderPoint(QPointF toward) const override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;
    std::optional<QSizeF> graphvizLayoutSize() const override;
    QStringList graphvizNodeAttributes(const Board &board, const QString &rankdir, qreal width,
                                       qreal height) const override;
    QString displayText() const override { return label; }
    QList<Property> properties() const override;
    QVariant propertyValue(const QString &id) const override;
    void setPropertyValue(const QString &id, const QVariant &value) override;

    // Where the text lives inside the outline, in world coordinates.
    QRectF textRect() const;
    const QStringList &lines() const { return lines_; }
    qreal minWidth() const { return minWidth_; }
    qreal minHeight() const { return minHeight_; }

    QString label;
    QString description;
    QString shape = QStringLiteral("rounded");
    QString tone = QStringLiteral("neutral");

private:
    qreal labelCenterY() const;
    QList<QPointF> polygonPoints() const;
    QPainterPath outline() const;

    QStringList lines_;
    std::optional<qreal> safeTextWidth_;
    std::optional<qreal> safeTextHeight_;
    qreal minWidth_ = 24;
    qreal minHeight_ = 20;
};

// A dashed grouping: it holds the elements whose parentBoundary names it and
// is laid out as a Graphviz cluster.
class FreeFrameShape : public Shape {
public:
    FreeFrameShape();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    void measure(const Board &board) override;
    void resizeTo(qreal width, qreal height, const Board &board) override;
    bool hitTest(QPointF point) const override;
    void onDoubleClick(Board &board) override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;
    bool drawBehindLinks() const override { return true; }
    bool staysInsideParent() const override { return true; }
    bool movesChildren() const override { return true; }
    bool isCreationContainer() const override { return true; }
    std::optional<QRectF> childBounds(const Board &board) const override;
    bool isGraphvizCluster() const override { return true; }
    QString layoutLabel() const override { return title; }
    // The band Graphviz leaves free at the top of the cluster is the one the
    // frame really paints; otherwise it reserves room for a smaller title and
    // the children are pushed down after the placement.
    qreal layoutLabelFontPx() const override;
    // The floor the frame takes back at its next measure, whatever Graphviz
    // deduced from its content: announced, it does not grow after the
    // placement into the neighboring frame.
    std::optional<QSizeF> layoutMinimumSize() const override;
    bool routingObstacle() const override { return false; }
    QString displayText() const override { return title; }
    QList<Property> properties() const override;
    QVariant propertyValue(const QString &id) const override;
    void setPropertyValue(const QString &id, const QVariant &value) override;

    const QStringList &titleLines() const { return titleLines_; }
    qreal titleHeight() const { return titleHeight_; }

    QString title;
    QString description;
    QString tone = QStringLiteral("neutral");

private:
    void wrapTitle();

    QStringList titleLines_;
    qreal titleHeight_ = 38;
    qreal minWidth_ = 220;
    qreal minHeight_ = 140;
};

// The invisible end of an edge that points at a free place of the board.
class FreeAnchorShape : public Shape {
public:
    FreeAnchorShape();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    bool resizable() const override { return false; }
    bool hitTest(QPointF) const override { return false; }
    void draw(QPainter &, const Board &, const ShapeState &) const override { }

    QString sourceEdgeId;
    QString endpoint; // "from" or "to"
};

// A free arrow or line, drawn by a press-drag-release: its points are
// relative to (x, y).
class FreeStrokeShape : public Shape {
public:
    FreeStrokeShape();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    bool resizable() const override { return false; }
    void measure(const Board &board) override;
    std::optional<QRectF> bounds(const Board &board) const override;
    bool hitTest(QPointF point) const override;
    void onDoubleClick(Board &board) override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;
    bool routingObstacle() const override { return false; }
    QString displayText() const override { return label; }
    QList<Property> properties() const override;
    QVariant propertyValue(const QString &id) const override;
    void setPropertyValue(const QString &id, const QVariant &value) override;

    // The stroke's extent with its label, world coordinates.
    QRectF strokeBounds() const;
    // The shaft actually stroked: stopped at the bases of the heads.
    std::vector<QPointF> shaft() const;

    std::vector<QPointF> points{{0, 0}, {120, 0}};
    QString label;
    QString description;
    QString tone = QStringLiteral("neutral");
    QString arrow = QStringLiteral("none"); // none | start | end | both
    QString line = QStringLiteral("solid"); // solid | dashed | dotted

private:
    void syncBounds();
    QPointF labelPoint() const;

    QRectF localBounds_;
    qreal labelWidth_ = 0;
};

class FreeEdge : public Link {
public:
    FreeEdge();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    QStringList graphvizAttributes(const Board &board) const override;
    void draw(QPainter &painter, const Board &board, bool selected) const override;
    QList<Property> properties() const override;
    QVariant propertyValue(const QString &id) const override;
    void setPropertyValue(const QString &id, const QVariant &value) override;

    QString description;
    QString tone = QStringLiteral("neutral");
    QString arrow = QStringLiteral("end");
    int appearAt = 0;
};

} // namespace diagram::explainer
