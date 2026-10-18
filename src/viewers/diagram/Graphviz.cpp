#include "Graphviz.h"

#include "Board.h"
#include "Family.h"

#include <QFontMetricsF>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <gvc.h>
#ifdef DUET_GRAPHVIZ_PLUGINS
#include <gvplugin.h>

extern "C" {
extern gvplugin_library_t gvplugin_core_LTX_library;
extern gvplugin_library_t gvplugin_dot_layout_LTX_library;
extern gvplugin_library_t gvplugin_neato_layout_LTX_library;
}
#endif

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace diagram::graphviz {

namespace {

constexpr qreal PT = 72;
const char *const metricFont = "Arial";

QString number(qreal value, int decimals = 3)
{
    return QString::number(value, 'f', decimals);
}

int silent(char *)
{
    return 0;
}

GVC_t *context()
{
    static GVC_t *gvc = [] {
        agseterr(AGMAX);
        agseterrf(silent);
#ifdef DUET_GRAPHVIZ_PLUGINS
        // dot and twopi/neato, and the JSON output: nothing loaded from the
        // system's plugin configuration.
        static lt_symlist_t builtins[] = {
            {"gvplugin_core_LTX_library", &gvplugin_core_LTX_library},
            {"gvplugin_dot_layout_LTX_library", &gvplugin_dot_layout_LTX_library},
            {"gvplugin_neato_layout_LTX_library", &gvplugin_neato_layout_LTX_library},
            {nullptr, nullptr},
        };
        return gvContextPlugins(builtins, 0);
#else
        return gvContext();
#endif
    }();
    return gvc;
}

QString endpoint(const QString &id, const GraphvizEndpoint *end)
{
    QString result = QLatin1Char('"') + dotEscape(end && !end->id.isEmpty() ? end->id : id) + QLatin1Char('"');
    if (end && !end->port.isEmpty())
        result += QStringLiteral(":\"") + dotEscape(end->port) + QLatin1Char('"');
    static const QSet<QString> compasses{
        QStringLiteral("n"),  QStringLiteral("ne"), QStringLiteral("e"),  QStringLiteral("se"), QStringLiteral("s"),
        QStringLiteral("sw"), QStringLiteral("w"),  QStringLiteral("nw"), QStringLiteral("c"),  QStringLiteral("_")};
    if (end && compasses.contains(end->compass))
        result += QLatin1Char(':') + end->compass;
    return result;
}

// The label as a fixed-size HTML table: Graphviz reserves exactly the pill
// the board paints.
QString fixedLabel(const QString &label, const Board &board, const QString &fontFace, qreal fontSize)
{
    const auto &o = board.style().linkLabel;
    const QStringList lines = label.split(QLatin1Char('\n'));
    const int width = std::max(1, int(std::ceil(text::widest(Font(o.fontPx), lines) + o.padX * 2)));
    const int height = std::max(1, int(std::ceil(lines.size() * (o.fontPx + 2) + o.padY * 2)));
    QStringList escaped;
    for (const QString &line : lines)
        escaped.append(dotHtmlEscape(line));
    QString content = escaped.join(QStringLiteral("<BR/>"));
    if (content.isEmpty())
        content = QStringLiteral("&#160;");
    return QStringLiteral("<TABLE BORDER=\"0\" CELLBORDER=\"0\" CELLSPACING=\"0\" CELLPADDING=\"0\" "
                          "FIXEDSIZE=\"TRUE\" WIDTH=\"%1\" HEIGHT=\"%2\"><TR><TD><FONT FACE=\"%3\" "
                          "POINT-SIZE=\"%4\">%5</FONT></TD></TR></TABLE>")
        .arg(width)
        .arg(height)
        .arg(dotHtmlEscape(fontFace))
        .arg(QString::number(fontSize > 0 ? fontSize : o.fontPx))
        .arg(content);
}

QStringList linkAttributes(const Link &link, const Board &board)
{
    QStringList attributes = link.graphvizAttributes(board);
    if (link.customWidth) {
        const LinkStyle style = link.drawStyle(board, false);
        qreal nativeSize = board.style().link.arrow / 10;
        for (const QString &attribute : attributes) {
            if (attribute.startsWith(QStringLiteral("arrowsize=")))
                nativeSize = attribute.mid(10).toDouble();
        }
        attributes.append(QStringLiteral("penwidth=") + number(*link.customWidth));
        attributes.append(QStringLiteral("arrowsize=") + number(nativeSize * style.markerScale));
    }
    return attributes;
}

std::vector<qreal> numbers(const QString &text)
{
    std::vector<qreal> result;
    for (const QString &part : text.split(QLatin1Char(','))) {
        bool ok = false;
        const qreal value = part.toDouble(&ok);
        if (!ok)
            return {};
        result.push_back(value);
    }
    return result;
}

// An edge's `pos`: "e,x,y [s,x,y] p0 p1 p2 …", a chain of cubic Béziers,
// converted to the board's frame (origin at the top).
std::optional<Spline> parseEdgePos(const QString &pos, qreal height)
{
    if (pos.isEmpty())
        return std::nullopt;
    Spline spline;
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    for (const QString &token : pos.trimmed().split(spaces, Qt::SkipEmptyParts)) {
        const QStringList parts = token.split(QLatin1Char(','));
        bool okX = false, okY = false;
        if (parts.size() == 3 && (parts[0] == QLatin1String("e") || parts[0] == QLatin1String("s"))) {
            const QPointF p(parts[1].toDouble(&okX), height - parts[2].toDouble(&okY));
            if (!okX || !okY)
                return std::nullopt;
            (parts[0] == QLatin1String("e") ? spline.end : spline.start) = p;
        } else if (parts.size() == 2) {
            const QPointF p(parts[0].toDouble(&okX), height - parts[1].toDouble(&okY));
            if (!okX || !okY)
                return std::nullopt;
            spline.points.push_back(p);
        }
    }
    if (spline.points.size() < 4 || (spline.points.size() - 1) % 3 != 0)
        return std::nullopt;
    return spline;
}

// Two links between the same pair come back on top of each other when only
// their routes were computed: push them aside, perpendicular to their chord
// and tapering to nothing at both ends.
std::vector<QPointF> spreadCurve(const std::vector<QPointF> &points, qreal offset)
{
    const size_t last = points.size() - 1;
    if (points.size() < 2 || !offset)
        return points;
    const qreal dx = points[last].x() - points[0].x(), dy = points[last].y() - points[0].y();
    const qreal length = std::hypot(dx, dy);
    if (!length)
        return points;
    const qreal nx = dy / length, ny = -dx / length;
    std::vector<QPointF> result;
    for (size_t i = 0; i < points.size(); ++i) {
        const qreal taper = std::sin(M_PI * qreal(i) / qreal(last));
        result.emplace_back(points[i].x() + nx * offset * taper, points[i].y() + ny * offset * taper);
    }
    return result;
}

QSizeF layoutSize(const Shape &shape)
{
    const std::optional<QSizeF> declared = shape.graphvizLayoutSize();
    return QSizeF(std::max<qreal>(1, declared ? declared->width() : shape.w),
                  std::max<qreal>(1, declared ? declared->height() : shape.h));
}

// The scale fitView() would give a layout of this bounding box.
qreal fitScale(const std::vector<qreal> &bb, QSizeF view, qreal maxScale)
{
    if (bb.size() != 4 || view.width() <= 0 || view.height() <= 0)
        return 0;
    const qreal shortSide = std::min(view.width(), view.height());
    const qreal inset = std::clamp(std::clamp(shortSide * 0.04, 16.0, 48.0), 0.0, shortSide / 4);
    return std::min({(view.width() - inset * 2) / std::max<qreal>(1, bb[2] - bb[0]),
                     (view.height() - inset * 2) / std::max<qreal>(1, bb[3] - bb[1]), maxScale});
}

} // namespace

QString dotEscape(const QString &text)
{
    QString result;
    for (const QChar c : text) {
        if (c == QLatin1Char('\\') || c == QLatin1Char('"'))
            result += QLatin1Char('\\');
        if (c == QLatin1Char('\n'))
            result += QStringLiteral("\\n");
        else
            result += c;
    }
    return result;
}

QString dotHtmlEscape(const QString &text)
{
    QString result = text;
    result.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    result.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    result.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    result.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return result;
}

qreal fontScale()
{
    // The gap between the board's font and Arial is not the same on lower
    // case, capitals, accents and digits: the sample mixes them.
    static const qreal scale = [] {
        const QString sample = QStringLiteral("Réorganiser le schéma ABCDEFGHIJ abcdefghijklm 0123456789");
        const qreal drawn = text::width(Font(100), sample);
        QFont arial(QString::fromLatin1(metricFont));
        arial.setPixelSize(100);
        const qreal estimated = QFontMetricsF(arial).horizontalAdvance(sample);
        if (!(drawn > 0) || !(estimated > 0))
            return 1.0;
        // Only ever reserve more, never less; bounded against odd fonts.
        return std::clamp(drawn / estimated, 1.0, 1.5);
    }();
    return scale;
}

QString dotFontSize(qreal px, qreal scale)
{
    if (!(px > 0))
        return QString::number(px);
    return QString::number(std::round(px * std::clamp(scale, 1.0, 1.5) * 100) / 100);
}

QByteArray layout(const QByteArray &dot, const char *engine, QString *error)
{
    GVC_t *gvc = context();
    Agraph_t *graph = agmemread(dot.constData());
    if (!graph) {
        if (error)
            *error = QStringLiteral("Graphviz could not read the graph");
        return {};
    }
    QByteArray result;
    if (gvLayout(gvc, graph, engine) == 0) {
        char *data = nullptr;
        size_t length = 0;
        if (gvRenderData(gvc, graph, "json", &data, &length) == 0)
            result = QByteArray(data, qsizetype(length));
        gvFreeRenderData(data);
        gvFreeLayout(gvc, graph);
    }
    agclose(graph);
    if (result.isEmpty() && error)
        *error = QStringLiteral("Graphviz could not lay the graph out");
    return result;
}

bool routeLinks(Board &board)
{
    if (board.links().empty())
        return false;
    board.measureAll();

    // A container is a backdrop and a free stroke decoration: neither
    // deflects a route, unless a link actually ends on it.
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
    if (routed.empty())
        return false;

    // Graphviz reads pinned positions from the bottom left.
    qreal flip = 0;
    for (const Shape *s : routed)
        flip = std::max(flip, s->y + s->h);
    flip += 1;
    QHash<QString, QSizeF> sizes;
    QString dot = QStringLiteral("digraph {\n  splines=true;\n  overlap=true;\n  inputscale=72;\n"
                                 "  node [shape=box fixedsize=true margin=0 label=\"\"];\n");
    for (const Shape *s : routed) {
        const QSizeF size = layoutSize(*s);
        sizes.insert(s->id, size);
        QStringList attrs = s->graphvizNodeAttributes(board, QStringLiteral("TB"), size.width(), size.height());
        if (attrs.isEmpty())
            attrs = {QStringLiteral("label=\"\""), QStringLiteral("margin=0"),
                     QStringLiteral("width=") + number(size.width() / PT),
                     QStringLiteral("height=") + number(size.height() / PT)};
        attrs.append(QStringLiteral("pos=\"%1,%2!\"")
                         .arg(number(s->x + size.width() / 2, 2), number(flip - (s->y + size.height() / 2), 2)));
        dot += QStringLiteral("  \"%1\" [%2];\n").arg(dotEscape(s->id), attrs.join(QLatin1Char(' ')));
    }
    QHash<QString, Link *> routable;
    for (const auto &l : board.links()) {
        if (l->from == l->to || l->routesItself() || !sizes.contains(l->from) || !sizes.contains(l->to))
            continue;
        QStringList attrs{QStringLiteral("id=\"%1\"").arg(dotEscape(l->id))};
        attrs += linkAttributes(*l, board);
        // Families attaching a relation to something more precise than a
        // node border (an ERD's columns) keep those endpoints here too.
        auto ends = l->graphvizEndpoints(board, QStringLiteral("LR"));
        if (ends) {
            const Shape *routeFrom = board.shape(ends->first.id.isEmpty() ? l->from : ends->first.id);
            const Shape *routeTo = board.shape(ends->second.id.isEmpty() ? l->to : ends->second.id);
            const QString rankdir =
                routeFrom && routeTo && routeTo->cx() < routeFrom->cx() ? QStringLiteral("RL") : QStringLiteral("LR");
            ends = l->graphvizEndpoints(board, rankdir);
        }
        dot += QStringLiteral("  %1 -> %2 [%3];\n")
                   .arg(endpoint(l->from, ends ? &ends->first : nullptr),
                        endpoint(l->to, ends ? &ends->second : nullptr), attrs.join(QLatin1Char(' ')));
        routable.insert(l->id, l.get());
    }
    dot += QStringLiteral("}\n");
    if (routable.isEmpty())
        return false;

    const QJsonObject g = QJsonDocument::fromJson(layout(dot.toUtf8(), "neato")).object();
    const std::vector<qreal> bb = numbers(g.value(QStringLiteral("bb")).toString());
    if (bb.size() != 4)
        return false;
    const qreal top = bb[3];

    // Graphviz recenters the drawing: every pinned node comes back moved by
    // the same vector, measured on one node.
    const Shape *reference = routed.front();
    std::vector<qreal> returned;
    for (const QJsonValue &object : g.value(QStringLiteral("objects")).toArray()) {
        if (object.toObject().value(QStringLiteral("name")).toString() == reference->id)
            returned = numbers(object.toObject().value(QStringLiteral("pos")).toString());
    }
    if (returned.size() != 2)
        return false;
    const QSizeF referenceSize = sizes.value(reference->id);
    const QPointF delta((reference->x + referenceSize.width() / 2) - returned[0],
                        (reference->y + referenceSize.height() / 2) - (top - returned[1]));
    int applied = 0;
    for (const QJsonValue &value : g.value(QStringLiteral("edges")).toArray()) {
        const QJsonObject edge = value.toObject();
        Link *l = routable.value(edge.value(QStringLiteral("id")).toString());
        if (!l)
            continue;
        std::optional<Spline> parsed = parseEdgePos(edge.value(QStringLiteral("pos")).toString(), top);
        if (!parsed)
            continue;
        const Shape *a = board.shape(l->from);
        const Shape *b = board.shape(l->to);
        // The global `dot` layout reserved the label's place; pinned `neato`
        // routing only refines the curve and keeps that place.
        std::optional<QPointF> labelPoint;
        if (const Spline *previous = l->validSpline(*a, *b))
            labelPoint = previous->labelPoint;
        if (!labelPoint)
            labelPoint = l->validLayoutLabelPoint(*a, *b);
        const qreal spread = l->parallelSpread(board) * (l->from < l->to ? 1 : -1);
        for (QPointF &p : parsed->points)
            p += delta;
        parsed->points = spreadCurve(parsed->points, spread);
        if (parsed->start)
            *parsed->start += delta;
        if (parsed->end)
            *parsed->end += delta;
        parsed->labelPoint = labelPoint;
        parsed->fromAnchor = QPointF(a->x, a->y);
        parsed->toAnchor = QPointF(b->x, b->y);
        l->spline = parsed;
        l->layoutLabelPoint.reset();
        ++applied;
    }
    if (applied) {
        board.resolveLabels();
        board.touch();
    }
    return applied > 0;
}

void placeNewcomers(Board &board, const QStringList &ids, const ArrangeOptions &options)
{
    if (ids.isEmpty())
        return;
    board.measureAll();
    const QSet<QString> newcomers(ids.begin(), ids.end());
    // The full layout, on a copy: where each shape stands relative to the
    // others.
    Board scratch;
    scratch.setFamily(board.family());
    scratch.setStyle(board.style());
    scratch.fromJson(board.toJson());
    scratch.meta = board.meta;
    QHash<QString, QPointF> laidOut;
    if (autoArrange(scratch, options)) {
        for (const auto &s : scratch.shapes())
            laidOut.insert(s->id, QPointF(s->x, s->y));
    }
    QSet<QString> placed;
    QRectF existing;
    for (const auto &s : board.shapes()) {
        if (newcomers.contains(s->id))
            continue;
        placed.insert(s->id);
        existing = existing.isNull() ? s->rect() : existing.united(s->rect());
    }
    // Breadth first from the placed shapes, so that a chain of new shapes
    // follows the neighbor it hangs from.
    QStringList pending = ids;
    qreal looseY = existing.top();
    for (int round = 0; !pending.isEmpty() && round <= ids.size(); ++round) {
        QStringList next;
        for (const QString &id : pending) {
            Shape *s = board.shape(id);
            if (!s)
                continue;
            Shape *anchor = nullptr;
            for (const auto &l : board.links()) {
                const QString other = l->from == id ? l->to : l->to == id ? l->from : QString();
                if (!other.isEmpty() && other != id && placed.contains(other)) {
                    anchor = board.shape(other);
                    break;
                }
            }
            if (!anchor && round < ids.size()) {
                next.append(id);
                continue;
            }
            if (anchor && laidOut.contains(id) && laidOut.contains(anchor->id)) {
                const QPointF offset = laidOut.value(id) - laidOut.value(anchor->id);
                s->x = anchor->x + offset.x();
                s->y = anchor->y + offset.y();
            } else if (anchor) {
                s->x = anchor->x;
                s->y = anchor->y + anchor->h + 60;
            } else {
                // Related to nothing placed: beside the diagram.
                s->x = (existing.isNull() ? 0 : existing.right()) + 80;
                s->y = looseY;
                looseY += s->h + 40;
            }
            // Off any overlap with what is already there.
            const auto overlaps = [&](const Shape *candidate) {
                const QRectF box = candidate->rect().adjusted(-12, -12, 12, 12);
                for (const auto &other : board.shapes()) {
                    if (other.get() != candidate && placed.contains(other->id) && !other->drawBehindLinks() &&
                        box.intersects(other->rect()))
                        return true;
                }
                return false;
            };
            for (int attempt = 0; attempt < 60 && overlaps(s); ++attempt)
                s->y += 24;
            board.containShape(*s, true);
            placed.insert(id);
            existing = existing.isNull() ? s->rect() : existing.united(s->rect());
        }
        pending = next;
    }
    board.resolveLabels();
    board.touch();
}

bool autoArrange(Board &board, const ArrangeOptions &options, QString *error)
{
    if (board.isEmpty())
        return true;
    // Shapes size themselves to their text in measure(): Graphviz gets the
    // real sizes.
    board.measureAll();
    const QByteArray base = board.snapshot();
    QString rankdir = options.rankdir.isEmpty() ? QStringLiteral("TB") : options.rankdir;
    const QString engine = options.engine.isEmpty() ? QStringLiteral("dot") : options.engine;

    QHash<QString, QSizeF> layoutSizes;
    for (const auto &s : board.shapes())
        layoutSizes.insert(s->id, layoutSize(*s));
    const Style &style = board.style();
    const qreal edgeWidth = std::max(0.1, style.link.width);
    const qreal arrowScale = std::max(0.1, style.link.arrow) / 10;
    const qreal scale = fontScale();
    const QString clusterFontSize = dotFontSize(ClusterLabelFontPx, scale);
    const QString edgeFontSize = dotFontSize(style.linkLabel.fontPx, scale);
    QString dot = QStringLiteral("digraph {\n  rankdir=%1;\n  nodesep=0.60;\n  ranksep=0.80;\n").arg(rankdir) +
                  QStringLiteral("  graph [fontname=\"%1\" fontsize=%2];\n").arg(metricFont, clusterFontSize) +
                  QStringLiteral("  node [shape=box fixedsize=true fontname=\"%1\"];\n").arg(metricFont) +
                  QStringLiteral("  edge [fontname=\"%1\" fontsize=%2 labelfloat=false penwidth=%3 arrowsize=%4];\n")
                      .arg(metricFont, edgeFontSize, number(edgeWidth), number(arrowScale));
    static const QSet<QString> splineModes{QStringLiteral("none"),     QStringLiteral("line"),
                                           QStringLiteral("polyline"), QStringLiteral("ortho"),
                                           QStringLiteral("curved"),   QStringLiteral("spline")};
    if (splineModes.contains(options.splines))
        dot += QStringLiteral("  splines=%1;\n").arg(options.splines);
    QString root = options.root;
    if (engine == QLatin1String("twopi")) {
        if (root.isEmpty()) {
            QSet<QString> targets;
            for (const auto &l : board.links())
                targets.insert(l->to);
            for (const auto &s : board.shapes()) {
                if (!targets.contains(s->id)) {
                    root = s->id;
                    break;
                }
            }
        }
        // overlap removal must not shrink a small radial layout into a
        // column.
        dot += QStringLiteral("  ranksep=2.2;\n  overlap=false;\n  overlap_shrink=false;\n");
        if (!root.isEmpty())
            dot += QStringLiteral("  root=\"%1\";\n").arg(dotEscape(root));
    }

    std::vector<Shape *> clusterCandidates;
    if (options.clusters) {
        for (const auto &s : board.shapes()) {
            if (s->isGraphvizCluster())
                clusterCandidates.push_back(s.get());
        }
    }
    QHash<QString, Shape *> candidatesByKey;
    for (Shape *boundary : clusterCandidates) {
        candidatesByKey.insert(boundary->id, boundary);
        if (!boundary->key.isEmpty())
            candidatesByKey.insert(boundary->key, boundary);
    }
    if (options.inferClusterMembership && !clusterCandidates.empty()) {
        // Some boards place a component visibly in a frame without writing
        // its parent: that intent is recovered from the geometry, choosing the
        // smallest enclosing frame, and never closing a cycle.
        const auto descendsFrom = [&](Shape *boundary, Shape *ancestor) {
            Shape *current = boundary;
            QSet<Shape *> seen;
            while (current && !seen.contains(current)) {
                if (current == ancestor)
                    return true;
                seen.insert(current);
                current = candidatesByKey.value(current->parentBoundary);
            }
            return false;
        };
        for (const auto &s : board.shapes()) {
            if (!s->staysInsideParent() || candidatesByKey.contains(s->parentBoundary))
                continue;
            const bool isCandidate =
                std::find(clusterCandidates.begin(), clusterCandidates.end(), s.get()) != clusterCandidates.end();
            Shape *parent = nullptr;
            for (Shape *boundary : clusterCandidates) {
                if (boundary == s.get() || descendsFrom(boundary, s.get()))
                    continue;
                if (isCandidate && !(boundary->w * boundary->h > s->w * s->h))
                    continue;
                if (s->cx() < boundary->x || s->cx() > boundary->x + boundary->w || s->cy() < boundary->y ||
                    s->cy() > boundary->y + boundary->h)
                    continue;
                if (!parent || boundary->w * boundary->h < parent->w * parent->h)
                    parent = boundary;
            }
            if (parent)
                s->parentBoundary = parent->key.isEmpty() ? parent->id : parent->key;
        }
    }
    // A parenthood that loops has no root: the link closing the loop is cut.
    for (const auto &s : board.shapes()) {
        QSet<Shape *> seen{s.get()};
        Shape *current = candidatesByKey.value(s->parentBoundary);
        while (current) {
            if (seen.contains(current)) {
                current->parentBoundary.clear();
                break;
            }
            seen.insert(current);
            current = candidatesByKey.value(current->parentBoundary);
        }
    }
    // An empty frame has nothing to give Graphviz as a cluster: it is
    // declared as a box like the others.
    QSet<QString> occupied;
    for (const auto &s : board.shapes()) {
        if (!s->parentBoundary.isEmpty())
            occupied.insert(s->parentBoundary);
    }
    std::vector<Shape *> boundaryShapes;
    for (Shape *boundary : clusterCandidates) {
        if (occupied.contains(boundary->id) || (!boundary->key.isEmpty() && occupied.contains(boundary->key)))
            boundaryShapes.push_back(boundary);
    }
    QHash<QString, Shape *> boundariesById, boundariesByKey;
    for (Shape *boundary : boundaryShapes) {
        boundariesById.insert(boundary->id, boundary);
        boundariesByKey.insert(boundary->id, boundary);
        if (!boundary->key.isEmpty())
            boundariesByKey.insert(boundary->key, boundary);
    }
    const auto isBoundary = [&](const Shape *s) { return boundariesById.contains(s->id); };
    std::vector<Shape *> ordinaryShapes;
    for (const auto &s : board.shapes()) {
        if (!isBoundary(s.get()))
            ordinaryShapes.push_back(s.get());
    }
    QSet<QString> emitted;
    const auto clusterAnchor = [](const Shape *boundary) { return QStringLiteral("__cluster_anchor_") + boundary->id; };
    const auto emitNode = [&](const Shape *s, const QString &indent) {
        emitted.insert(s->id);
        const QSizeF size = layoutSizes.value(s->id);
        const QStringList custom = s->graphvizNodeAttributes(board, rankdir, size.width(), size.height());
        if (!custom.isEmpty())
            dot += QStringLiteral("%1\"%2\" [%3];\n").arg(indent, dotEscape(s->id), custom.join(QLatin1Char(' ')));
        else
            dot += QStringLiteral("%1\"%2\" [label=\"\" margin=0 width=%3 height=%4];\n")
                       .arg(indent, dotEscape(s->id), number(size.width() / PT), number(size.height() / PT));
    };
    const auto parentOf = [&](const Shape *s) { return boundariesByKey.value(s->parentBoundary); };
    QSet<QString> emittedClusters;
    std::function<void(Shape *, const QString &)> emitCluster = [&](Shape *boundary, const QString &indent) {
        if (emittedClusters.contains(boundary->id))
            return;
        emittedClusters.insert(boundary->id);
        dot +=
            QStringLiteral("%1subgraph \"%2\" {\n").arg(indent, dotEscape(QStringLiteral("cluster_") + boundary->id));
        const qreal labelPx = boundary->layoutLabelFontPx() > 0 ? boundary->layoutLabelFontPx() : ClusterLabelFontPx;
        dot += QStringLiteral("%1  label=\"%2\"; fontsize=%3; margin=%4;\n")
                   .arg(indent, dotEscape(boundary->layoutLabel()), dotFontSize(labelPx, scale),
                        QString::number(ClusterMargin));
        dot += QStringLiteral("%1  \"%2\" [shape=point width=0 height=0 label=\"\" style=invis];\n")
                   .arg(indent, dotEscape(clusterAnchor(boundary)));
        for (Shape *child : boundaryShapes) {
            if (parentOf(child) == boundary)
                emitCluster(child, indent + QStringLiteral("  "));
        }
        for (Shape *child : ordinaryShapes) {
            if (parentOf(child) == boundary)
                emitNode(child, indent + QStringLiteral("  "));
        }
        dot += indent + QStringLiteral("}\n");
    };
    if (!boundaryShapes.empty()) {
        dot += QStringLiteral("  compound=true;\n");
        // A frame's inner margin is taken from the rank separation: it is
        // given back, so that neighboring frames stay apart.
        const qreal clusterGap = (2 * ClusterMargin) / PT;
        dot += QStringLiteral("  nodesep=%1;\n").arg(number(0.60 + clusterGap));
        dot += QStringLiteral("  ranksep=%1;\n").arg(number(0.80 + clusterGap));
        for (Shape *boundary : boundaryShapes) {
            if (!parentOf(boundary))
                emitCluster(boundary, QStringLiteral("  "));
        }
        // Whatever the parenthood, every frame is declared.
        for (Shape *boundary : boundaryShapes)
            emitCluster(boundary, QStringLiteral("  "));
    }
    for (const Shape *s : ordinaryShapes) {
        if (!emitted.contains(s->id))
            emitNode(s, QStringLiteral("  "));
    }

    // Labels go in the DOT so that dot reserves their place: their `lp`
    // is reused as is, collisions being resolved over the whole diagram.
    std::vector<Link *> layoutLinks;
    QHash<QString, Link *> layoutLinksById;
    QHash<QString, std::pair<QString, QString>> layoutEndpointsById;
    for (const auto &l : board.links()) {
        QStringList attrs{QStringLiteral("id=\"%1\"").arg(dotEscape(l->id))};
        attrs += linkAttributes(*l, board);
        const QString layoutLabel = l->layoutLabel(board);
        if (!layoutLabel.isEmpty()) {
            const QString fontName = l->graphvizFontName().trimmed();
            const qreal fontSize = l->graphvizFontSize();
            if (!fontName.isEmpty())
                attrs.append(QStringLiteral("fontname=\"%1\"").arg(dotEscape(fontName)));
            if (fontSize > 0)
                attrs.append(QStringLiteral("fontsize=") + dotFontSize(fontSize, scale));
            QString html = l->graphvizHtmlLabel(board);
            if (html.isEmpty())
                html = fixedLabel(layoutLabel, board, fontName.isEmpty() ? QString::fromLatin1(metricFont) : fontName,
                                  fontSize > 0 ? fontSize : style.linkLabel.fontPx);
            attrs.append(QStringLiteral("label=<") + html + QLatin1Char('>'));
        }
        if (!board.shape(l->from) || !board.shape(l->to))
            continue;
        auto ends = l->graphvizEndpoints(board, rankdir);
        const auto clusterEndpoint = [&](const QString &shapeId, std::optional<GraphvizEndpoint> end) {
            const QString endpointId = end && !end->id.isEmpty() ? end->id : shapeId;
            Shape *boundary = boundariesById.value(endpointId);
            if (!boundary)
                return end;
            GraphvizEndpoint result = end.value_or(GraphvizEndpoint{});
            result.id = clusterAnchor(boundary);
            return std::optional<GraphvizEndpoint>(result);
        };
        const std::optional<GraphvizEndpoint> fromEnd =
            clusterEndpoint(l->from, ends ? std::optional(ends->first) : std::nullopt);
        const std::optional<GraphvizEndpoint> toEnd =
            clusterEndpoint(l->to, ends ? std::optional(ends->second) : std::nullopt);
        dot += QStringLiteral("  %1 -> %2 [%3];\n")
                   .arg(endpoint(l->from, fromEnd ? &*fromEnd : nullptr), endpoint(l->to, toEnd ? &*toEnd : nullptr),
                        attrs.join(QLatin1Char(' ')));
        layoutLinks.push_back(l.get());
        layoutLinksById.insert(l->id, l.get());
        layoutEndpointsById.insert(l->id, {fromEnd && !fromEnd->id.isEmpty() ? fromEnd->id : l->from,
                                           toEnd && !toEnd->id.isEmpty() ? toEnd->id : l->to});
    }

    // Graphviz ranks only what edges relate and packs the rest on one row:
    // disconnected pieces are chained with invisible edges so that they
    // stack in the requested direction.
    if (options.stackDisconnected) {
        std::vector<Shape *> rankable;
        for (Shape *s : ordinaryShapes) {
            if (emitted.contains(s->id))
                rankable.push_back(s);
        }
        QHash<QString, QString> roots;
        for (Shape *s : rankable)
            roots.insert(s->id, s->id);
        const std::function<QString(QString)> find = [&](QString id) {
            while (roots.value(id) != id)
                id = roots.value(id);
            return id;
        };
        const auto unite = [&](const QString &left, const QString &right) {
            const QString a = find(left), b = find(right);
            if (a != b)
                roots.insert(a, b);
        };
        for (const auto &l : board.links()) {
            if (roots.contains(l->from) && roots.contains(l->to))
                unite(l->from, l->to);
        }
        // A frame stays whole: its content is one link of the chain.
        const auto outermost = [&](const Shape *s) {
            const Shape *current = s;
            const Shape *boundary = nullptr;
            QSet<const Shape *> seen;
            while (current && !seen.contains(current)) {
                seen.insert(current);
                const Shape *next = parentOf(current);
                if (!next)
                    break;
                boundary = next;
                current = next;
            }
            return boundary;
        };
        QHash<QString, QString> byBoundary;
        for (Shape *s : rankable) {
            const Shape *boundary = outermost(s);
            if (!boundary)
                continue;
            if (byBoundary.contains(boundary->id))
                unite(byBoundary.value(boundary->id), s->id);
            else
                byBoundary.insert(boundary->id, s->id);
        }
        std::map<QString, std::vector<Shape *>> groups;
        QStringList order;
        for (Shape *s : rankable) {
            const QString r = find(s->id);
            if (!groups.count(r))
                order.append(r);
            groups[r].push_back(s);
        }
        if (groups.size() > 1) {
            QSet<QString> incoming, outgoing;
            for (const auto &l : board.links()) {
                if (!roots.contains(l->from) || !roots.contains(l->to))
                    continue;
                outgoing.insert(l->from);
                incoming.insert(l->to);
            }

            struct Group {
                qreal top, left;
                Shape *entry;
                Shape *exit;
            };

            std::vector<Group> ordered;
            for (const QString &r : order) {
                std::vector<Shape *> members = groups[r];
                std::sort(members.begin(), members.end(),
                          [](const Shape *a, const Shape *b) { return a->y != b->y ? a->y < b->y : a->x < b->x; });
                Group group{members.front()->y, members.front()->x, members.front(), members.back()};
                for (Shape *s : members) {
                    group.top = std::min(group.top, s->y);
                    group.left = std::min(group.left, s->x);
                }
                for (Shape *s : members) {
                    if (!incoming.contains(s->id)) {
                        group.entry = s;
                        break;
                    }
                }
                for (auto it = members.rbegin(); it != members.rend(); ++it) {
                    if (!outgoing.contains((*it)->id)) {
                        group.exit = *it;
                        break;
                    }
                }
                ordered.push_back(group);
            }
            std::stable_sort(ordered.begin(), ordered.end(), [](const Group &a, const Group &b) {
                return a.top != b.top ? a.top < b.top : a.left < b.left;
            });
            for (size_t i = 1; i < ordered.size(); ++i)
                dot += QStringLiteral("  \"%1\" -> \"%2\" [style=invis];\n")
                           .arg(dotEscape(ordered[i - 1].exit->id), dotEscape(ordered[i].entry->id));
        }
    }
    dot += QStringLiteral("}\n");

    const QByteArray engineName = engine.toLatin1();
    QByteArray output = layout(dot.toUtf8(), engineName.constData(), error);
    if (output.isEmpty())
        return false;
    QJsonObject g = QJsonDocument::fromJson(output).object();
    // For families with no prescribed reading direction (C4): the better
    // framed of the two complete layouts.
    if (options.adaptiveDirection && engine == QLatin1String("dot") &&
        (rankdir == QLatin1String("LR") || rankdir == QLatin1String("TB"))) {
        const qreal current = fitScale(numbers(g.value(QStringLiteral("bb")).toString()), options.viewSize,
                                       std::min(style.maxScale, style.fitMaxScale));
        if (current > 0 && current < 1) {
            const QString other = rankdir == QLatin1String("LR") ? QStringLiteral("TB") : QStringLiteral("LR");
            QString alternativeDot = dot;
            alternativeDot.replace(QStringLiteral("rankdir=%1;").arg(rankdir),
                                   QStringLiteral("rankdir=%1;").arg(other));
            const QByteArray alternative = layout(alternativeDot.toUtf8(), engineName.constData());
            if (!alternative.isEmpty()) {
                const QJsonObject candidate = QJsonDocument::fromJson(alternative).object();
                const qreal scaleOther = fitScale(numbers(candidate.value(QStringLiteral("bb")).toString()),
                                                  options.viewSize, std::min(style.maxScale, style.fitMaxScale));
                if (scaleOther >= current * 1.15)
                    g = candidate;
            }
        }
    }
    const std::vector<qreal> bb = numbers(g.value(QStringLiteral("bb")).toString());
    if (bb.size() != 4) {
        if (error)
            *error = QStringLiteral("Graphviz returned no drawing");
        return false;
    }
    const qreal height = bb[3] - bb[1];
    std::vector<std::pair<Shape *, QPointF>> targets;
    QHash<Shape *, size_t> targetIndex;
    const auto setTarget = [&](Shape *s, QPointF p) {
        if (targetIndex.contains(s)) {
            targets[targetIndex.value(s)].second = p;
            return;
        }
        targetIndex.insert(s, targets.size());
        targets.emplace_back(s, p);
    };
    QStringList names;
    for (const QJsonValue &value : g.value(QStringLiteral("objects")).toArray()) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        names.append(name);
        if (options.clusters && name.startsWith(QStringLiteral("cluster_")) && object.contains(QStringLiteral("bb"))) {
            Shape *boundary = board.shape(name.mid(8));
            if (!boundary)
                continue;
            const std::vector<qreal> box = numbers(object.value(QStringLiteral("bb")).toString());
            if (box.size() != 4)
                continue;
            // Graphviz sizes a cluster on its content alone, without the floor
            // the frame keeps (its title): it is opened here around the
            // center Graphviz chose.
            const std::optional<QSizeF> minimum = boundary->layoutMinimumSize();
            const qreal boxWidth = std::max(box[2] - box[0], minimum ? minimum->width() : 0.0);
            const qreal boxHeight = std::max(box[3] - box[1], minimum ? minimum->height() : 0.0);
            boundary->w = boxWidth;
            boundary->h = boxHeight;
            setTarget(boundary, QPointF(box[0] - (boxWidth - (box[2] - box[0])) / 2,
                                        (height - box[3]) - (boxHeight - (box[3] - box[1])) / 2));
            continue;
        }
        Shape *s = board.shape(name);
        const std::vector<qreal> pos = numbers(object.value(QStringLiteral("pos")).toString());
        if (s && pos.size() == 2) {
            const QSizeF size = layoutSizes.value(s->id, QSizeF(s->w, s->h));
            setTarget(s, QPointF(pos[0] - size.width() / 2, (height - pos[1]) - size.height() / 2));
        }
    }

    // Graphviz guarantees geometric membership; the board may reserve a
    // little more inside a frame (its title): the containment rule of
    // dragging runs on the final positions.
    {
        std::vector<std::pair<Shape *, QPointF>> starts;
        for (const auto &s : board.shapes())
            starts.emplace_back(s.get(), QPointF(s->x, s->y));
        for (const auto &[s, p] : targets) {
            s->x = p.x();
            s->y = p.y();
        }
        const auto depth = [&board](const Shape *s) {
            int value = 0;
            const Shape *current = s;
            QSet<const Shape *> seen;
            while (current && !current->parentBoundary.isEmpty() && !seen.contains(current)) {
                seen.insert(current);
                current = board.shapeByReference(current->parentBoundary);
                if (current)
                    ++value;
            }
            return value;
        };
        std::vector<Shape *> contained;
        for (const auto &s : board.shapes()) {
            if (s->staysInsideParent() && !s->parentBoundary.isEmpty())
                contained.push_back(s.get());
        }
        std::stable_sort(contained.begin(), contained.end(),
                         [&depth](const Shape *a, const Shape *b) { return depth(a) < depth(b); });
        for (Shape *s : contained)
            board.containShape(*s);
        for (auto &[s, p] : targets)
            p = QPointF(s->x, s->y);
        for (const auto &[s, p] : starts) {
            s->x = p.x();
            s->y = p.y();
        }
    }

    // Final positions, then curves: dot's edges (pos) and the places it
    // reserved for their labels (lp), anchored at the final positions.
    for (const auto &[s, p] : targets) {
        s->x = p.x();
        s->y = p.y();
    }
    const QJsonArray edges = g.value(QStringLiteral("edges")).toArray();
    for (qsizetype i = 0; i < edges.size(); ++i) {
        const QJsonObject edge = edges[i].toObject();
        Link *l = layoutLinksById.value(edge.value(QStringLiteral("id")).toString());
        if (!l && size_t(i) < layoutLinks.size())
            l = layoutLinks[size_t(i)];
        if (!l)
            continue;
        const auto expected = layoutEndpointsById.value(l->id);
        const int tail = edge.value(QStringLiteral("tail")).toInt(-1);
        const int head = edge.value(QStringLiteral("head")).toInt(-1);
        if (tail < 0 || head < 0 || tail >= names.size() || head >= names.size() || names[tail] != expected.first ||
            names[head] != expected.second)
            continue;
        l->spline.reset();
        const Shape *a = board.shape(l->from);
        const Shape *b = board.shape(l->to);
        const std::vector<qreal> lp = numbers(edge.value(QStringLiteral("lp")).toString());
        const std::optional<QPointF> labelPoint =
            lp.size() == 2 ? std::optional<QPointF>(QPointF(lp[0], height - lp[1])) : std::nullopt;
        // A relation touching a frame was routed to its invisible anchor: the
        // board draws another line, so the reserved label place is dropped
        // with the curve.
        const bool touchesCluster = boundariesById.contains(l->from) || boundariesById.contains(l->to);
        if (labelPoint && a && b && !touchesCluster)
            l->layoutLabelPoint = Link::ReservedLabel{*labelPoint, QPointF(a->x, a->y), QPointF(b->x, b->y)};
        else
            l->layoutLabelPoint.reset();
        // twopi (mind maps) keeps its organic arcs; self-loops their loop.
        if (engine != QLatin1String("twopi") && !touchesCluster && l->from != l->to && a && b) {
            std::optional<Spline> parsed = parseEdgePos(edge.value(QStringLiteral("pos")).toString(), height);
            if (parsed) {
                parsed->labelPoint = labelPoint;
                parsed->fromAnchor = QPointF(a->x, a->y);
                parsed->toAnchor = QPointF(b->x, b->y);
                l->spline = parsed;
            }
        }
    }
    board.resolveLabels();
    board.pushHistory(base);
    board.touch();
    return true;
}

} // namespace diagram::graphviz
