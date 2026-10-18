#include "GraphvizArrangeDot.h"

#include "Board.h"
#include "GraphvizFormat.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <optional>
#include <ranges>

namespace diagram::graphviz {

namespace {

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

// --- Full layout (autoArrange): the DOT header

// The root of a radial layout: the given one, else the first shape no link
// points to.
QString radialRoot(const Board &board, const QString &given)
{
    if (!given.isEmpty())
        return given;
    QSet<QString> targets;
    for (const auto &l : board.links())
        targets.insert(l->to);
    const auto isRoot = [&](const auto &s) { return !targets.contains(s->id); };
    const auto root = std::ranges::find_if(board.shapes(), isRoot);
    if (root == board.shapes().end())
        return {};
    return (*root)->id;
}

QString radialSettings(const Board &board, const QString &givenRoot)
{
    const QString root = radialRoot(board, givenRoot);
    // overlap removal must not shrink a small radial layout into a column.
    QString dot = QStringLiteral("  ranksep=2.2;\n  overlap=false;\n  overlap_shrink=false;\n");
    if (!root.isEmpty())
        dot += QStringLiteral("  root=\"%1\";\n").arg(dotEscape(root));
    return dot;
}

QString dotHeader(const Board &board, const ArrangeOptions &options, const QString &rankdir, const QString &engine)
{
    const Style &style = board.style();
    const qreal edgeWidth = std::max(0.1, style.link.width);
    const qreal arrowScale = std::max(0.1, style.link.arrow) / 10;
    const qreal scale = fontScale();
    const QString clusterFontSize = dotFontSize(clusterLabelFontPx, scale);
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
    if (engine == QLatin1String("twopi"))
        dot += radialSettings(board, options.root);
    return dot;
}

// --- Full layout: frames as clusters

std::vector<Shape *> clusterCandidates(const Board &board, bool clusters)
{
    std::vector<Shape *> candidates;
    if (!clusters)
        return candidates;
    for (const auto &s : board.shapes()) {
        if (s->isGraphvizCluster())
            candidates.push_back(s.get());
    }
    return candidates;
}

// Frames by id and by key: what a shape's parentBoundary may name.
QHash<QString, Shape *> indexByReference(const std::vector<Shape *> &boundaries)
{
    QHash<QString, Shape *> index;
    for (Shape *boundary : boundaries) {
        index.insert(boundary->id, boundary);
        if (!boundary->key.isEmpty())
            index.insert(boundary->key, boundary);
    }
    return index;
}

bool descendsFrom(const Shape *boundary, const Shape *ancestor, const QHash<QString, Shape *> &framesByReference)
{
    const Shape *current = boundary;
    QSet<const Shape *> seen;
    while (current && !seen.contains(current)) {
        if (current == ancestor)
            return true;
        seen.insert(current);
        current = framesByReference.value(current->parentBoundary);
    }
    return false;
}

bool containsCenterOf(const Shape &boundary, const Shape &s)
{
    const bool isOutside = s.cx() < boundary.x || s.cx() > boundary.x + boundary.w || s.cy() < boundary.y ||
                           s.cy() > boundary.y + boundary.h;
    return !isOutside;
}

// The smallest frame visibly holding `s` that would not close a cycle, or
// none. A frame is only ever held by a larger one.
const Shape *smallestEnclosingFrame(const Shape &s,
                                    const std::vector<Shape *> &candidates,
                                    const QHash<QString, Shape *> &framesByReference)
{
    const bool isFrame = std::ranges::find(candidates, &s) != candidates.end();
    const auto area = [](const Shape &shape) { return shape.w * shape.h; };
    const Shape *parent = nullptr;
    for (const Shape *boundary : candidates) {
        if (boundary == &s || descendsFrom(boundary, &s, framesByReference))
            continue;
        const bool isLarger = area(*boundary) > area(s);
        if (isFrame && !isLarger)
            continue;
        if (!containsCenterOf(*boundary, s))
            continue;
        if (!parent || area(*boundary) < area(*parent))
            parent = boundary;
    }
    return parent;
}

// Some boards place a component visibly in a frame without writing its
// parent: that intent is recovered from the geometry, choosing the smallest
// enclosing frame, and never closing a cycle.
void inferClusterMembership(const Board &board,
                            const std::vector<Shape *> &candidates,
                            const QHash<QString, Shape *> &framesByReference)
{
    for (const auto &s : board.shapes()) {
        if (!s->staysInsideParent() || framesByReference.contains(s->parentBoundary))
            continue;
        const Shape *parent = smallestEnclosingFrame(*s, candidates, framesByReference);
        if (parent)
            s->parentBoundary = parent->key.isEmpty() ? parent->id : parent->key;
    }
}

// A parenthood that loops has no root: the link closing the loop is cut.
void cutParenthoodLoops(const Board &board, const QHash<QString, Shape *> &framesByReference)
{
    for (const auto &s : board.shapes()) {
        QSet<Shape *> seen{s.get()};
        Shape *current = framesByReference.value(s->parentBoundary);
        while (current) {
            if (seen.contains(current)) {
                current->parentBoundary.clear();
                break;
            }
            seen.insert(current);
            current = framesByReference.value(current->parentBoundary);
        }
    }
}

// An empty frame has nothing to give Graphviz as a cluster: it is declared as
// a box like the others.
ClusterSet occupiedClusters(const Board &board, const std::vector<Shape *> &candidates)
{
    QSet<QString> occupied;
    for (const auto &s : board.shapes()) {
        if (!s->parentBoundary.isEmpty())
            occupied.insert(s->parentBoundary);
    }
    ClusterSet clusters;
    for (Shape *boundary : candidates) {
        const bool isKeyOccupied = !boundary->key.isEmpty() && occupied.contains(boundary->key);
        if (occupied.contains(boundary->id) || isKeyOccupied)
            clusters.boundaries.push_back(boundary);
    }
    for (Shape *boundary : clusters.boundaries)
        clusters.byId.insert(boundary->id, boundary);
    clusters.byReference = indexByReference(clusters.boundaries);
    for (const auto &s : board.shapes()) {
        if (!clusters.byId.contains(s->id))
            clusters.ordinaryShapes.push_back(s.get());
    }
    return clusters;
}

ClusterSet prepareClusters(const Board &board, const ArrangeOptions &options)
{
    const std::vector<Shape *> candidates = clusterCandidates(board, options.clusters);
    const QHash<QString, Shape *> candidatesByReference = indexByReference(candidates);
    if (options.inferClusterMembership && !candidates.empty())
        inferClusterMembership(board, candidates, candidatesByReference);
    cutParenthoodLoops(board, candidatesByReference);
    return occupiedClusters(board, candidates);
}

QString clusterAnchor(const Shape &boundary)
{
    return QStringLiteral("__cluster_anchor_") + boundary.id;
}

// --- Full layout: stacking disconnected pieces

// Union-find over shape ids.
class Components {
public:
    void add(const QString &id) { roots_.insert(id, id); }

    [[nodiscard]] bool contains(const QString &id) const { return roots_.contains(id); }

    [[nodiscard]] QString find(QString id) const
    {
        while (roots_.value(id) != id)
            id = roots_.value(id);
        return id;
    }

    void unite(const QString &left, const QString &right)
    {
        const QString a = find(left);
        const QString b = find(right);
        if (a != b)
            roots_.insert(a, b);
    }

private:
    QHash<QString, QString> roots_;
};

const Shape *outermostFrame(const Shape *s, const ClusterSet &clusters)
{
    const Shape *current = s;
    const Shape *boundary = nullptr;
    QSet<const Shape *> seen;
    while (current && !seen.contains(current)) {
        seen.insert(current);
        const Shape *next = clusters.parentOf(current);
        if (!next)
            break;
        boundary = next;
        current = next;
    }
    return boundary;
}

// A frame stays whole: its content is one link of the chain.
void uniteFrameContents(Components &components, const std::vector<Shape *> &rankable, const ClusterSet &clusters)
{
    QHash<QString, QString> byBoundary;
    for (const Shape *s : rankable) {
        const Shape *boundary = outermostFrame(s, clusters);
        if (!boundary)
            continue;
        if (byBoundary.contains(boundary->id))
            components.unite(byBoundary.value(boundary->id), s->id);
        else
            byBoundary.insert(boundary->id, s->id);
    }
}

// The connected pieces of `rankable`, in the order their first shape comes.
std::vector<std::vector<Shape *>> connectedPieces(const Board &board,
                                                  const std::vector<Shape *> &rankable,
                                                  const ClusterSet &clusters)
{
    Components components;
    for (const Shape *s : rankable)
        components.add(s->id);
    for (const auto &l : board.links()) {
        if (components.contains(l->from) && components.contains(l->to))
            components.unite(l->from, l->to);
    }
    uniteFrameContents(components, rankable, clusters);
    std::vector<std::vector<Shape *>> pieces;
    QHash<QString, size_t> pieceIndex;
    for (Shape *s : rankable) {
        const QString root = components.find(s->id);
        if (!pieceIndex.contains(root)) {
            pieceIndex.insert(root, pieces.size());
            pieces.emplace_back();
        }
        pieces[pieceIndex.value(root)].push_back(s);
    }
    return pieces;
}

struct LinkEnds {
    QSet<QString> sources;
    QSet<QString> targets;
};

// A piece of the chain: where it stands, and the shapes the invisible edges
// enter and leave it by.
struct ChainPiece {
    qreal top = 0;
    qreal left = 0;
    Shape *entry = nullptr;
    Shape *exit = nullptr;
};

ChainPiece chainPiece(std::vector<Shape *> members, const LinkEnds &ends)
{
    std::sort(members.begin(), members.end(),
              [](const Shape *a, const Shape *b) { return a->y != b->y ? a->y < b->y : a->x < b->x; });
    ChainPiece piece{members.front()->y, members.front()->x, members.front(), members.back()};
    for (const Shape *s : members) {
        piece.top = std::min(piece.top, s->y);
        piece.left = std::min(piece.left, s->x);
    }
    const auto entry = std::ranges::find_if(members, [&](const Shape *s) { return !ends.targets.contains(s->id); });
    if (entry != members.end())
        piece.entry = *entry;
    const auto reversed = std::views::reverse(members);
    const auto exit = std::ranges::find_if(reversed, [&](const Shape *s) { return !ends.sources.contains(s->id); });
    if (exit != reversed.end())
        piece.exit = *exit;
    return piece;
}

// --- Full layout: the DOT body

QString endpointId(const std::optional<GraphvizEndpoint> &end, const QString &shapeId)
{
    return end && !end->id.isEmpty() ? end->id : shapeId;
}

// Writes the nodes, clusters and edges of the full layout.
class DotBody {
public:
    DotBody(const Board &board, const ClusterSet &clusters, QString rankdir, const QHash<QString, QSizeF> &layoutSizes)
        : board_(board)
        , clusters_(clusters)
        , rankdir_(std::move(rankdir))
        , layoutSizes_(layoutSizes)
    {
    }

    void addShapes()
    {
        if (!clusters_.boundaries.empty()) {
            dot_ += QStringLiteral("  compound=true;\n");
            // A frame's inner margin is taken from the rank separation: it is
            // given back, so that neighboring frames stay apart.
            const qreal clusterGap = (2 * clusterMargin) / pointsPerInch;
            dot_ += QStringLiteral("  nodesep=%1;\n").arg(number(0.60 + clusterGap));
            dot_ += QStringLiteral("  ranksep=%1;\n").arg(number(0.80 + clusterGap));
            for (Shape *boundary : clusters_.boundaries) {
                if (!clusters_.parentOf(boundary))
                    addCluster(boundary, QStringLiteral("  "));
            }
            // Whatever the parenthood, every frame is declared.
            for (Shape *boundary : clusters_.boundaries)
                addCluster(boundary, QStringLiteral("  "));
        }
        for (const Shape *s : clusters_.ordinaryShapes) {
            if (!emitted_.contains(s->id))
                addNode(s, QStringLiteral("  "));
        }
    }

    // Labels go in the DOT so that dot reserves their place: their `lp` is
    // reused as is, collisions being resolved over the whole diagram.
    LaidOutLinks addLinks()
    {
        LaidOutLinks links;
        for (const auto &l : board_.links()) {
            QStringList attrs{QStringLiteral("id=\"%1\"").arg(dotEscape(l->id))};
            attrs += linkAttributes(*l, board_);
            attrs += labelAttributes(*l);
            if (!board_.shape(l->from) || !board_.shape(l->to))
                continue;
            const auto ends = l->graphvizEndpoints(board_, rankdir_);
            const std::optional<GraphvizEndpoint> fromEnd =
                clusterEndpoint(l->from, ends ? std::optional(ends->first) : std::nullopt);
            const std::optional<GraphvizEndpoint> toEnd =
                clusterEndpoint(l->to, ends ? std::optional(ends->second) : std::nullopt);
            dot_ += QStringLiteral("  %1 -> %2 [%3];\n")
                        .arg(endpoint(l->from, fromEnd ? &*fromEnd : nullptr),
                             endpoint(l->to, toEnd ? &*toEnd : nullptr), attrs.join(QLatin1Char(' ')));
            links.inOrder.push_back(l.get());
            links.byId.insert(l->id, l.get());
            links.endpointsById.insert(l->id, {endpointId(fromEnd, l->from), endpointId(toEnd, l->to)});
        }
        return links;
    }

    // Graphviz ranks only what edges relate and packs the rest on one row:
    // disconnected pieces are chained with invisible edges so that they
    // stack in the requested direction.
    void addStacking()
    {
        std::vector<Shape *> rankable;
        for (Shape *s : clusters_.ordinaryShapes) {
            if (emitted_.contains(s->id))
                rankable.push_back(s);
        }
        const std::vector<std::vector<Shape *>> pieces = connectedPieces(board_, rankable, clusters_);
        if (pieces.size() <= 1)
            return;
        const LinkEnds ends = linkEndsWithin(rankable);
        std::vector<ChainPiece> ordered;
        ordered.reserve(pieces.size());
        for (const std::vector<Shape *> &piece : pieces)
            ordered.push_back(chainPiece(piece, ends));
        std::stable_sort(ordered.begin(), ordered.end(), [](const ChainPiece &a, const ChainPiece &b) {
            return a.top != b.top ? a.top < b.top : a.left < b.left;
        });
        for (size_t i = 1; i < ordered.size(); ++i)
            dot_ += QStringLiteral("  \"%1\" -> \"%2\" [style=invis];\n")
                        .arg(dotEscape(ordered[i - 1].exit->id), dotEscape(ordered[i].entry->id));
    }

    [[nodiscard]] const QString &text() const { return dot_; }

private:
    void addNode(const Shape *s, const QString &indent)
    {
        emitted_.insert(s->id);
        const QSizeF size = layoutSizes_.value(s->id);
        const QStringList custom = s->graphvizNodeAttributes(board_, rankdir_, size.width(), size.height());
        if (!custom.isEmpty())
            dot_ += QStringLiteral("%1\"%2\" [%3];\n").arg(indent, dotEscape(s->id), custom.join(QLatin1Char(' ')));
        else
            dot_ += QStringLiteral("%1\"%2\" [label=\"\" margin=0 width=%3 height=%4];\n")
                        .arg(indent, dotEscape(s->id), number(size.width() / pointsPerInch),
                             number(size.height() / pointsPerInch));
    }

    void addCluster(Shape *boundary, const QString &indent)
    {
        if (emittedClusters_.contains(boundary->id))
            return;
        emittedClusters_.insert(boundary->id);
        dot_ +=
            QStringLiteral("%1subgraph \"%2\" {\n").arg(indent, dotEscape(QStringLiteral("cluster_") + boundary->id));
        const qreal labelPx = boundary->layoutLabelFontPx() > 0 ? boundary->layoutLabelFontPx() : clusterLabelFontPx;
        dot_ += QStringLiteral("%1  label=\"%2\"; fontsize=%3; margin=%4;\n")
                    .arg(indent, dotEscape(boundary->layoutLabel()), dotFontSize(labelPx, scale_),
                         QString::number(clusterMargin));
        dot_ += QStringLiteral("%1  \"%2\" [shape=point width=0 height=0 label=\"\" style=invis];\n")
                    .arg(indent, dotEscape(clusterAnchor(*boundary)));
        const QString childIndent = indent + QStringLiteral("  ");
        for (Shape *child : clusters_.boundaries) {
            if (clusters_.parentOf(child) == boundary)
                addCluster(child, childIndent);
        }
        for (const Shape *child : clusters_.ordinaryShapes) {
            if (clusters_.parentOf(child) == boundary)
                addNode(child, childIndent);
        }
        dot_ += indent + QStringLiteral("}\n");
    }

    QStringList labelAttributes(const Link &l) const
    {
        const QString layoutLabel = l.layoutLabel(board_);
        if (layoutLabel.isEmpty())
            return {};
        QStringList attrs;
        const QString fontName = l.graphvizFontName().trimmed();
        const qreal fontSize = l.graphvizFontSize();
        if (!fontName.isEmpty())
            attrs.append(QStringLiteral("fontname=\"%1\"").arg(dotEscape(fontName)));
        if (fontSize > 0)
            attrs.append(QStringLiteral("fontsize=") + dotFontSize(fontSize, scale_));
        QString html = l.graphvizHtmlLabel(board_);
        if (html.isEmpty())
            html = fixedLabel(layoutLabel, board_, fontName.isEmpty() ? QString::fromLatin1(metricFont) : fontName,
                              fontSize > 0 ? fontSize : board_.style().linkLabel.fontPx);
        attrs.append(QStringLiteral("label=<") + html + QLatin1Char('>'));
        return attrs;
    }

    // A relation ending on a frame is routed to the frame's invisible anchor.
    std::optional<GraphvizEndpoint> clusterEndpoint(const QString &shapeId,
                                                    const std::optional<GraphvizEndpoint> &end) const
    {
        const Shape *boundary = clusters_.byId.value(endpointId(end, shapeId));
        if (!boundary)
            return end;
        GraphvizEndpoint result = end.value_or(GraphvizEndpoint{});
        result.id = clusterAnchor(*boundary);
        return result;
    }

    LinkEnds linkEndsWithin(const std::vector<Shape *> &shapes) const
    {
        QSet<QString> ids;
        for (const Shape *s : shapes)
            ids.insert(s->id);
        LinkEnds ends;
        for (const auto &l : board_.links()) {
            if (!ids.contains(l->from) || !ids.contains(l->to))
                continue;
            ends.sources.insert(l->from);
            ends.targets.insert(l->to);
        }
        return ends;
    }

    const Board &board_;
    const ClusterSet &clusters_;
    QString rankdir_;
    const QHash<QString, QSizeF> &layoutSizes_;
    qreal scale_ = fontScale();
    QSet<QString> emitted_;
    QSet<QString> emittedClusters_;
    QString dot_;
};

} // namespace

ArrangeDot writeArrangeDot(const Board &board, const ArrangeOptions &options, const QHash<QString, QSizeF> &layoutSizes)
{
    const QString header = dotHeader(board, options, options.rankdir, options.engine);
    ClusterSet clusters = prepareClusters(board, options);
    DotBody body(board, clusters, options.rankdir, layoutSizes);
    body.addShapes();
    LaidOutLinks links = body.addLinks();
    if (options.stackDisconnected)
        body.addStacking();
    QString text = header + body.text() + QStringLiteral("}\n");
    return {std::move(text), std::move(clusters), std::move(links)};
}

} // namespace diagram::graphviz
