#pragma once

#include "Style.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVariant>

#include <functional>
#include <optional>
#include <vector>

class QPainter;

// The elements of a diagram, ported from sidecar's shape-board.js: Shape and
// Link are the base classes every diagram family derives its elements from.
// As in sidecar, an element is built from plain properties (a JSON object:
// the `props` of its constructor) and serializes back to them (toJSON); the
// board's history, clipboard and files go through that same path.
//
// Coordinates are world units (pixels at zoom 1), y downwards.
namespace diagram {

class Board;

// A connection port: where a link can be dragged from or attached.
struct Port {
    qreal x = 0;
    qreal y = 0;
    QString key; // a family's own port (a column, a lifeline), empty for the four sides
};

struct ShapeState {
    bool selected = false;
    bool editing = false; // its text is under the in-place editor
    bool dropTarget = false;
};

// In-place text editing, as beginTextEdit() takes it in sidecar.
struct TextEditSpec {
    std::function<QString()> get;
    std::function<void(const QString &)> set;
    QColor color;
    Font font;
    qreal lineHeight = 20;
    Align align = Align::Left;
    // The editor's place, in world units.
    std::function<QRectF()> rect;
    // Enter ends the edit (Shift+Enter: a new line); false for texts made
    // of several lines (a class and its members), where Enter is a new line
    // and Ctrl+Enter ends the edit.
    bool enterCommits = true;
};

// A property shown in the inspector, beside the text edited in place.
struct Property {
    enum class Kind {
        Text,   // a line
        Lines,  // several lines (attributes, methods)
        Choice, // one of `choices`
        Bool,
        Number,
    };
    QString id;
    QString label;
    Kind kind = Kind::Text;
    // (value, label) pairs of a choice.
    QList<std::pair<QString, QString>> choices;
    qreal minimum = 0;
    qreal maximum = 100;
};

// ---------------------------------------------------------------------------
// Shape

class Shape {
public:
    Shape(const QString &type, qreal width = 120, qreal height = 60);
    virtual ~Shape();

    // Reads the properties the shape was created with (the JS constructor's
    // `props`). Subclasses read their own after calling the base.
    virtual void load(const QJsonObject &props);
    virtual QJsonObject toJson() const;

    qreal cx() const { return x + w / 2; }
    qreal cy() const { return y + h / 2; }
    QRectF rect() const { return QRectF(x, y, w, h); }

    // --- Size
    virtual bool resizable() const { return true; }
    virtual void resizeTo(qreal width, qreal height, const Board &board);
    // Recomputes the size from the content (text growth…). Default: none.
    virtual void measure(const Board &board);
    // The smallest size a resize can reach; default: the measured size.
    virtual std::optional<QSizeF> resizeMinimum(const Board &board) const;
    // A note grows downwards while typed; others stay centered.
    virtual bool editResizeAnchorTopLeft() const { return false; }

    // --- Connections
    virtual bool showConnectionPorts() const { return false; }
    virtual bool showConnectionPortsOnHover() const { return false; }
    // In world coordinates. Default: the middles of the four sides.
    virtual QList<Port> ports(const Board &board) const;
    virtual QList<Port> connectionPorts(const Board &board) const { return ports(board); }
    virtual bool connectionHitTest(const Board &board, QPointF point) const;
    // Where a link being dragged from `from` meets this shape.
    virtual QPointF connectionPoint(const Board &board, QPointF from, QPointF pointer) const;
    // The port of this shape under `point`, for a link released on it.
    virtual std::optional<Port> connectionPortAt(const Board &board, QPointF point) const;

    virtual bool hitTest(QPointF point) const;
    // Where the ray from the center towards `toward` crosses the outline.
    // Rectangle by default; other outlines override it.
    virtual QPointF borderPoint(QPointF toward) const;

    // --- Life cycle
    virtual void onAdded(Board &board);
    // Double-click: usually starts editing the text (board.beginTextEdit).
    virtual void onDoubleClick(Board &board);

    // --- Drawing
    // The body, in world coordinates. Base: a rounded rectangle, shadowed.
    virtual void draw(QPainter &painter, const Board &board, const ShapeState &state) const;
    // Bounds wider than the box (a lifeline under its header).
    virtual std::optional<QRectF> bounds(const Board &board) const;
    // Containers drawn under the links.
    virtual bool drawBehindLinks() const { return false; }

    // --- Containers
    // Whether the shape is kept inside its parent frame.
    virtual bool staysInsideParent() const { return false; }
    // Whether moving the shape carries its children along.
    virtual bool movesChildren() const { return false; }
    // Whether a shape can be created or dropped into it.
    virtual bool isCreationContainer() const { return false; }
    // Where children may lie, in world coordinates.
    virtual std::optional<QRectF> childBounds(const Board &board) const;
    // Shapes Graphviz lays out as clusters (subgraphs).
    virtual bool isGraphvizCluster() const { return false; }
    virtual QString layoutLabel() const { return {}; }
    // The font size of the cluster's title; 0 for Graphviz's default.
    virtual qreal layoutLabelFontPx() const { return 0; }
    virtual std::optional<QSizeF> layoutMinimumSize() const { return std::nullopt; }

    // --- Layout
    // A box Graphviz should reserve instead of the drawn one.
    virtual std::optional<QSizeF> graphvizLayoutSize() const { return std::nullopt; }
    // The node's DOT attributes, replacing the default fixed-size box.
    virtual QStringList graphvizNodeAttributes(const Board &board, const QString &rankdir, qreal width,
                                               qreal height) const;
    // Whether links are routed around the shape.
    virtual bool routingObstacle() const { return true; }

    // --- Text and properties
    // What the shape is called, for menus and descriptions (its text, name,
    // label or key).
    virtual QString displayText() const;
    virtual QList<Property> properties() const { return {}; }
    virtual QVariant propertyValue(const QString &id) const;
    virtual void setPropertyValue(const QString &id, const QVariant &value);
    // An image dropped or pasted on it (notes); false if refused.
    virtual bool addImage(Board &board, const QByteArray &dataUrl);

    QString id;
    QString type;
    // Stable identifier carried over from the source (a Mermaid node id): the
    // serializers reuse it so that an edited diagram keeps its ids.
    QString key;
    qreal x = 0;
    qreal y = 0;
    qreal w = 120;
    qreal h = 60;
    QString customFill;      // "#rrggbb" chosen by the user, or empty
    QString parentBoundary;  // id or key of the frame holding it, or empty
    // Order in which an explainer reveals it (0: none).
    int appearAt = 0;

    // Measure caches (measureShape()).
    std::optional<qreal> manualWidth;
    std::optional<qreal> manualHeight;
    qreal resizeMinWidth = 1;
    qreal resizeMinHeight = 1;
    bool applyingMeasuredSize = false;

protected:
    // The base fill: the custom fill if any, else `fallback`.
    QColor fillColor(const QColor &fallback) const;
    // A painted shape's outline is its fill with 25 % black.
    QColor strokeColor(const QColor &fallback) const;
    // Native ink until a custom fill requires automatic contrast.
    QColor textColor(const QColor &fallback) const;
};

// measureShape(): the shape measures its content, then keeps a size chosen
// by hand if it is larger than the content needs.
void measureShape(const Board &board, Shape &shape, const QJsonObject *props = nullptr);

// ---------------------------------------------------------------------------
// Link

// A curve computed by the layout (Graphviz): a chain of cubic Béziers (1 + 3n
// points), valid while both ends stay where they were.
struct Spline {
    std::vector<QPointF> points;
    std::optional<QPointF> start; // Graphviz's `s,` point (tail arrow)
    std::optional<QPointF> end;   // `e,` point: the tip of the head
    std::optional<QPointF> labelPoint;
    QPointF fromAnchor; // the ends' positions when it was computed
    QPointF toAnchor;

    QJsonObject toJson() const;
    static std::optional<Spline> fromJson(const QJsonValue &value);
};

// The route actually drawn.
struct Route {
    QPointF a;
    QPointF b;
    std::vector<QPointF> curve;  // Béziers; empty for a straight line
    std::vector<QPointF> points; // a polyline instead (family routes)
    std::optional<QPointF> labelPoint;
};

struct LinkStyle {
    QColor color;
    qreal width = 2;
    qreal arrow = 9;
    qreal markerScale = 1;
};

// Where a link ends for Graphviz: another node, a record port, a compass.
struct GraphvizEndpoint {
    QString id;
    QString port;
    QString compass;
};

class Link {
public:
    explicit Link(const QString &type);
    virtual ~Link();

    virtual void load(const QJsonObject &props);
    virtual QJsonObject toJson() const;

    // Endpoints border to border, or the layout's curve while it holds.
    virtual std::optional<Route> route(const Board &board) const;
    // The layout's curve, or nothing if an end has moved since (it is then
    // dropped).
    const Spline *validSpline(const Shape &a, const Shape &b) const;
    std::optional<QPointF> validLayoutLabelPoint(const Shape &a, const Shape &b) const;
    // The polyline actually drawn.
    std::vector<QPointF> routePolyline(const Route &route) const;
    qreal distanceToRoute(const Route &route, QPointF point) const;
    virtual bool hitTest(const Board &board, QPointF point, qreal tolerance) const;

    // The label's pill as painted.
    struct LabelBox {
        QStringList lines;
        qreal lineHeight = 14;
        qreal width = 0;
        qreal height = 0;
    };
    virtual LabelBox labelBox(const Board &board) const;
    // Where the label lives: on the line, at the middle of the curve, or the
    // place the layout reserved for it.
    virtual QPointF labelPos(const Route &route, const Board &board) const;

    // The body, stopped at the markers' bases.
    struct BodyOptions {
        QColor color;
        qreal width = 2;
        QList<qreal> dash;
        qreal trim = 0;      // at the head
        qreal trimStart = 0; // at the tail
    };
    void strokeBody(QPainter &painter, const Route &route, const BodyOptions &options) const;
    qreal arrivalAngle(const Route &route) const;
    qreal departureAngle(const Route &route) const;
    // This link's rank among those joining the same two shapes, both ways.
    struct ParallelGroup {
        int index = 0;
        int count = 1;
    };
    ParallelGroup parallelGroup(const Board &board) const;
    // The offset of this link from the pair's axis; families with tall
    // labels (state transitions) widen it.
    virtual qreal parallelSpread(const Board &board) const;

    // Color, width and marker scale, after the user's choices and selection.
    virtual LinkStyle drawStyle(const Board &board, bool selected) const;
    LinkStyle drawStyle(const Board &board, bool selected, qreal nativeWidth) const;
    // The line and its head, under the shapes.
    virtual void draw(QPainter &painter, const Board &board, bool selected) const;
    // The label's pill, above the shapes.
    virtual void drawLabel(QPainter &painter, const Board &board, bool editing) const;
    void drawLabelAt(QPainter &painter, const Board &board, const Route &route) const;
    void drawEndpoints(QPainter &painter, const Board &board) const;
    virtual std::optional<QRectF> bounds(const Board &board) const;

    // --- Layout
    virtual QStringList graphvizAttributes(const Board &board) const;
    // The text Graphviz reserves room for (the label by default).
    virtual QString layoutLabel(const Board &board) const;
    // An HTML label replacing the default fixed-size box (multi-part labels).
    virtual QString graphvizHtmlLabel(const Board &board) const;
    virtual QString graphvizFontName() const { return {}; }
    virtual qreal graphvizFontSize() const { return 0; }
    // Endpoints more precise than the nodes (columns, lifelines).
    virtual std::optional<std::pair<GraphvizEndpoint, GraphvizEndpoint>>
    graphvizEndpoints(const Board &board, const QString &rankdir) const;
    // A link drawing a route of its own (a sequence message along its lane):
    // Graphviz's routing leaves it alone.
    virtual bool routesItself() const { return false; }

    // --- Text and properties
    virtual QList<Property> properties() const { return {}; }
    virtual QVariant propertyValue(const QString &id) const;
    virtual void setPropertyValue(const QString &id, const QVariant &value);
    // Double-click: edits the label in place.
    virtual void onDoubleClick(Board &board);

    QString id;
    QString type;
    QString from;
    QString to;
    QString key;
    QString label;
    QString customLineStyle; // "solid", "dashed", "dotted" or empty
    QString customColor;     // "#rrggbb" or empty
    std::optional<qreal> customWidth;
    std::optional<Spline> spline;
    // A label place reserved by the layout, kept apart from the curve.
    struct ReservedLabel {
        QPointF point;
        QPointF fromAnchor;
        QPointF toAnchor;
    };
    std::optional<ReservedLabel> layoutLabelPoint;

protected:
    std::optional<QPointF> reservedLabelPoint(const Route &route, const Board &board) const;
    std::optional<QPointF> labelShift(const Route &route, const Board &board) const;
    const std::vector<QPointF> &flatCurve(const std::vector<QPointF> &curve) const;

private:
    mutable std::vector<QPointF> flatFor_;
    mutable std::vector<QPointF> flat_;
};

// The dash a user's line style asks for, over a family's own.
QList<qreal> linkDash(const Link &link, const QList<qreal> &fallback = {});
// A filled arrowhead with its tip at `tip`, pointing along `angle`.
void fillArrowHead(QPainter &painter, QPointF tip, qreal angle, qreal size, const QColor &color,
                   qreal spread = GraphvizNormalArrowSpread);
QPointF bezierPoint(QPointF p0, QPointF p1, QPointF p2, QPointF p3, qreal t);
qreal distanceToSegment(QPointF point, QPointF a, QPointF b);
QPointF polylineMidpoint(const std::vector<QPointF> &points);
std::vector<QPointF> trimPolyline(const std::vector<QPointF> &points, qreal trimStart, qreal trimEnd);
// Intersection of the ray from `center` towards `toward` with a polygon.
QPointF polygonBorderPoint(QPointF center, QPointF toward, const QList<QPointF> &points);
// Intersection of that ray with an ellipse of radii rx, ry.
QPointF ellipseBorderPoint(QPointF center, QPointF toward, qreal rx, qreal ry);

// JSON helpers for the families' load().
QString jsonString(const QJsonObject &props, const QString &name, const QString &fallback = {});
qreal jsonNumber(const QJsonObject &props, const QString &name, qreal fallback = 0);
bool jsonBool(const QJsonObject &props, const QString &name, bool fallback = false);
QStringList jsonStrings(const QJsonObject &props, const QString &name);
QJsonArray toJsonArray(const QStringList &values);

} // namespace diagram
