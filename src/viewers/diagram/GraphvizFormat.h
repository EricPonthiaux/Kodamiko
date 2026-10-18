#pragma once

#include <QJsonObject>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

// The pieces of Graphviz's languages shared by every layout: DOT written for
// the board's shapes and links, JSON read back into the board's frame.
namespace diagram {

class Board;
class Link;
class Shape;
struct GraphvizEndpoint;
struct Spline;

namespace graphviz {

constexpr qreal pointsPerInch = 72;
// The font Graphviz measures text with (see fontScale()).
inline constexpr const char *metricFont = "Arial";

// A number as DOT writes it.
QString number(qreal value, int decimals = 3);
// An edge end in DOT: the shape `id`, or the endpoint's node, port and compass.
QString endpoint(const QString &id, const GraphvizEndpoint *end);
// The link's own DOT attributes, with its custom width applied.
QStringList linkAttributes(const Link &link, const Board &board);
// The comma-separated numbers of `text`; empty if one is malformed.
std::vector<qreal> numbers(const QString &text);
// An edge's `pos`: "e,x,y [s,x,y] p0 p1 p2 …", a chain of cubic Béziers,
// converted to the board's frame (origin at the top) of `height`.
std::optional<Spline> parseEdgePos(const QString &pos, qreal height);
// The size Graphviz lays the shape out at.
QSizeF layoutSize(const Shape &shape);
// The "bb" of a graph or cluster: x0, y0, x1, y1, or empty.
std::vector<qreal> boundingBox(const QJsonObject &object);

} // namespace graphviz
} // namespace diagram
