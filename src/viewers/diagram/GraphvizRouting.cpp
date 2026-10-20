// SPDX-License-Identifier: GPL-3.0-only

#include "Board.h"
#include "Graphviz.h"
#include "GraphvizFormat.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <cmath>

namespace diagram::graphviz {

namespace {

// Two links between the same pair come back on top of each other when only
// their routes were computed: push them aside, perpendicular to their chord
// and tapering to nothing at both ends.
std::vector<QPointF> spreadCurve(const std::vector<QPointF> &points, qreal offset)
{
    const size_t last = points.size() - 1;
    if (points.size() < 2 || !offset)
        return points;
    const qreal dx = points[last].x() - points[0].x();
    const qreal dy = points[last].y() - points[0].y();
    const qreal length = std::hypot(dx, dy);
    if (!length)
        return points;
    const qreal nx = dy / length;
    const qreal ny = -dx / length;
    std::vector<QPointF> result;
    for (size_t i = 0; i < points.size(); ++i) {
        const qreal taper = std::sin(M_PI * qreal(i) / qreal(last));
        result.emplace_back(points[i].x() + nx * offset * taper, points[i].y() + ny * offset * taper);
    }
    return result;
}

// --- Routing alone (routeLinks)

// A container is a backdrop and a free stroke decoration: neither deflects a
// route, unless a link actually ends on it.
std::vector<Shape *> routedShapes(const Board &board)
{
    QSet<QString> endpoints;
    for (const auto &l : board.links()) {
        endpoints.insert(l->from);
        endpoints.insert(l->to);
    }
    std::vector<Shape *> routed;
    for (const auto &s : board.shapes()) {
        if (endpoints.contains(s->id) || s->routingObstacle())
            routed.push_back(s.get());
    }
    return routed;
}

QString pinnedNode(const Board &board, const Shape &s, QSizeF size, qreal flip)
{
    QStringList attrs = s.graphvizNodeAttributes(board, QStringLiteral("TB"), size.width(), size.height());
    if (attrs.isEmpty())
        attrs = {QStringLiteral("label=\"\""), QStringLiteral("margin=0"),
                 QStringLiteral("width=") + number(size.width() / pointsPerInch),
                 QStringLiteral("height=") + number(size.height() / pointsPerInch)};
    attrs.append(QStringLiteral("pos=\"%1,%2!\"")
                     .arg(number(s.x + size.width() / 2, 2), number(flip - (s.y + size.height() / 2), 2)));
    return QStringLiteral("  \"%1\" [%2];\n").arg(dotEscape(s.id), attrs.join(QLatin1Char(' ')));
}

QString routedEdge(const Board &board, const Link &l)
{
    QStringList attrs{QStringLiteral("id=\"%1\"").arg(dotEscape(l.id))};
    attrs += linkAttributes(l, board);
    // Families attaching a relation to something more precise than a node
    // border (an ERD's columns) keep those endpoints here too.
    auto ends = l.graphvizEndpoints(board, QStringLiteral("LR"));
    if (ends) {
        const Shape *routeFrom = board.shape(ends->first.id.isEmpty() ? l.from : ends->first.id);
        const Shape *routeTo = board.shape(ends->second.id.isEmpty() ? l.to : ends->second.id);
        const bool goesLeft = routeFrom && routeTo && routeTo->cx() < routeFrom->cx();
        const QString rankdir = goesLeft ? QStringLiteral("RL") : QStringLiteral("LR");
        ends = l.graphvizEndpoints(board, rankdir);
    }
    return QStringLiteral("  %1 -> %2 [%3];\n")
        .arg(endpoint(l.from, ends ? &ends->first : nullptr), endpoint(l.to, ends ? &ends->second : nullptr),
             attrs.join(QLatin1Char(' ')));
}

// Every routed node pinned where it is, and the links between them.
struct PinnedGraph {
    QString dot;
    QHash<QString, QSizeF> sizes;
    QHash<QString, Link *> routable;
};

PinnedGraph pinnedGraph(const Board &board, const std::vector<Shape *> &routed)
{
    // Graphviz reads pinned positions from the bottom left.
    qreal flip = 0;
    for (const Shape *s : routed)
        flip = std::max(flip, s->y + s->h);
    flip += 1;
    PinnedGraph graph;
    graph.dot = QStringLiteral("digraph {\n  splines=true;\n  overlap=true;\n  inputscale=72;\n"
                               "  node [shape=box fixedsize=true margin=0 label=\"\"];\n");
    for (const Shape *s : routed) {
        const QSizeF size = layoutSize(*s);
        graph.sizes.insert(s->id, size);
        graph.dot += pinnedNode(board, *s, size, flip);
    }
    for (const auto &l : board.links()) {
        const bool joinsRouted = graph.sizes.contains(l->from) && graph.sizes.contains(l->to);
        if (l->from == l->to || l->routesItself() || !joinsRouted)
            continue;
        graph.dot += routedEdge(board, *l);
        graph.routable.insert(l->id, l.get());
    }
    graph.dot += QStringLiteral("}\n");
    return graph;
}

// Graphviz recenters the drawing: every pinned node comes back moved by the
// same vector, measured on one node.
std::optional<QPointF> recenteringDelta(const QJsonObject &g, const Shape &reference, QSizeF referenceSize, qreal top)
{
    std::vector<qreal> returned;
    for (const QJsonValue &object : g.value(QStringLiteral("objects")).toArray()) {
        if (object.toObject().value(QStringLiteral("name")).toString() == reference.id)
            returned = numbers(object.toObject().value(QStringLiteral("pos")).toString());
    }
    if (returned.size() != 2)
        return std::nullopt;
    return QPointF((reference.x + referenceSize.width() / 2) - returned[0],
                   (reference.y + referenceSize.height() / 2) - (top - returned[1]));
}

void applyRoute(const Board &board, Link &l, Spline route, QPointF delta)
{
    const Shape *a = board.shape(l.from);
    const Shape *b = board.shape(l.to);
    // The global `dot` layout reserved the label's place; pinned `neato`
    // routing only refines the curve and keeps that place.
    std::optional<QPointF> labelPoint;
    if (const Spline *previous = l.validSpline(*a, *b))
        labelPoint = previous->labelPoint;
    if (!labelPoint)
        labelPoint = l.validLayoutLabelPoint(*a, *b);
    const qreal spread = l.parallelSpread(board) * (l.from < l.to ? 1 : -1);
    for (QPointF &p : route.points)
        p += delta;
    route.points = spreadCurve(route.points, spread);
    if (route.start)
        *route.start += delta;
    if (route.end)
        *route.end += delta;
    route.labelPoint = labelPoint;
    route.fromAnchor = QPointF(a->x, a->y);
    route.toAnchor = QPointF(b->x, b->y);
    l.spline = std::move(route);
    l.layoutLabelPoint.reset();
}

} // namespace

bool routeLinks(Board &board)
{
    if (board.links().empty())
        return false;
    board.measureAll();
    const std::vector<Shape *> routed = routedShapes(board);
    if (routed.empty())
        return false;
    const PinnedGraph graph = pinnedGraph(board, routed);
    if (graph.routable.isEmpty())
        return false;

    const QJsonObject g = QJsonDocument::fromJson(layout(graph.dot.toUtf8(), "neato")).object();
    const std::vector<qreal> bb = boundingBox(g);
    if (bb.size() != 4)
        return false;
    const qreal top = bb[3];
    const Shape *reference = routed.front();
    const std::optional<QPointF> delta = recenteringDelta(g, *reference, graph.sizes.value(reference->id), top);
    if (!delta)
        return false;
    int applied = 0;
    for (const QJsonValue &value : g.value(QStringLiteral("edges")).toArray()) {
        const QJsonObject edge = value.toObject();
        Link *l = graph.routable.value(edge.value(QStringLiteral("id")).toString());
        if (!l)
            continue;
        std::optional<Spline> parsed = parseEdgePos(edge.value(QStringLiteral("pos")).toString(), top);
        if (!parsed)
            continue;
        applyRoute(board, *l, std::move(*parsed), *delta);
        ++applied;
    }
    if (applied) {
        board.resolveLabels();
        board.touch();
    }
    return applied > 0;
}

} // namespace diagram::graphviz
