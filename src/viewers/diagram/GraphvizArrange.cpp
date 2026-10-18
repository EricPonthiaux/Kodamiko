#include "Board.h"
#include "Graphviz.h"
#include "GraphvizArrangeDot.h"
#include "GraphvizFormat.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <optional>

namespace diagram::graphviz {

namespace {

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

// --- Full layout: reading Graphviz's answer

std::optional<QJsonObject> runLayout(const QString &dot, const QString &engine, QString *error)
{
    const QByteArray engineName = engine.toLatin1();
    const QByteArray output = layout(dot.toUtf8(), engineName.constData(), error);
    if (output.isEmpty())
        return std::nullopt;
    return QJsonDocument::fromJson(output).object();
}

// The view a layout is framed in.
struct Framing {
    QSizeF view;
    qreal maxScale = 1;
};

// For families with no prescribed reading direction (C4): the better framed
// of the layouts in `rankdir` and in the other of LR and TB.
QJsonObject betterFramed(const QJsonObject &laidOut, const QString &dot, const QString &rankdir, Framing framing)
{
    const qreal current = fitScale(boundingBox(laidOut), framing.view, framing.maxScale);
    const bool isShrunk = current > 0 && current < 1;
    if (!isShrunk)
        return laidOut;
    const QString other = rankdir == QLatin1String("LR") ? QStringLiteral("TB") : QStringLiteral("LR");
    QString alternativeDot = dot;
    alternativeDot.replace(QStringLiteral("rankdir=%1;").arg(rankdir), QStringLiteral("rankdir=%1;").arg(other));
    const QByteArray alternative = layout(alternativeDot.toUtf8(), "dot");
    if (alternative.isEmpty())
        return laidOut;
    const QJsonObject candidate = QJsonDocument::fromJson(alternative).object();
    const qreal scaleOther = fitScale(boundingBox(candidate), framing.view, framing.maxScale);
    return scaleOther >= current * 1.15 ? candidate : laidOut;
}

// The positions the layout gives shapes, in the order Graphviz names them.
class Targets {
public:
    void set(Shape *s, QPointF p)
    {
        if (index_.contains(s)) {
            targets_[index_.value(s)].second = p;
            return;
        }
        index_.insert(s, targets_.size());
        targets_.emplace_back(s, p);
    }

    // Moves every shape to its target.
    void apply() const
    {
        for (const auto &[s, p] : targets_) {
            s->x = p.x();
            s->y = p.y();
        }
    }

    // Takes the shapes' current positions as their targets.
    void retake()
    {
        for (auto &[s, p] : targets_)
            p = QPointF(s->x, s->y);
    }

private:
    std::vector<std::pair<Shape *, QPointF>> targets_;
    QHash<Shape *, size_t> index_;
};

// Graphviz sizes a cluster on its content alone, without the floor the frame
// keeps (its title): it is opened here around the center Graphviz chose.
void placeCluster(const Board &board, Targets &targets, const QJsonObject &object, qreal height)
{
    Shape *boundary = board.shape(object.value(QStringLiteral("name")).toString().mid(8));
    if (!boundary)
        return;
    const std::vector<qreal> box = boundingBox(object);
    if (box.size() != 4)
        return;
    const std::optional<QSizeF> minimum = boundary->layoutMinimumSize();
    const qreal boxWidth = std::max(box[2] - box[0], minimum ? minimum->width() : 0.0);
    const qreal boxHeight = std::max(box[3] - box[1], minimum ? minimum->height() : 0.0);
    boundary->w = boxWidth;
    boundary->h = boxHeight;
    targets.set(boundary, QPointF(box[0] - (boxWidth - (box[2] - box[0])) / 2,
                                  (height - box[3]) - (boxHeight - (box[3] - box[1])) / 2));
}

struct ReadBackFrame {
    qreal height = 0;
    bool clusters = false;
};

Targets readTargets(const Board &board,
                    const QJsonArray &objects,
                    const QHash<QString, QSizeF> &layoutSizes,
                    ReadBackFrame frame)
{
    Targets targets;
    for (const QJsonValue &value : objects) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        const bool isCluster = name.startsWith(QStringLiteral("cluster_")) && object.contains(QStringLiteral("bb"));
        if (frame.clusters && isCluster) {
            placeCluster(board, targets, object, frame.height);
            continue;
        }
        Shape *s = board.shape(name);
        const std::vector<qreal> pos = numbers(object.value(QStringLiteral("pos")).toString());
        if (s && pos.size() == 2) {
            const QSizeF size = layoutSizes.value(s->id, QSizeF(s->w, s->h));
            targets.set(s, QPointF(pos[0] - size.width() / 2, (frame.height - pos[1]) - size.height() / 2));
        }
    }
    return targets;
}

QStringList objectNames(const QJsonArray &objects)
{
    QStringList names;
    for (const QJsonValue &value : objects)
        names.append(value.toObject().value(QStringLiteral("name")).toString());
    return names;
}

int nestingDepth(const Board &board, const Shape *s)
{
    int depth = 0;
    const Shape *current = s;
    QSet<const Shape *> seen;
    while (current && !current->parentBoundary.isEmpty() && !seen.contains(current)) {
        seen.insert(current);
        current = board.shapeByReference(current->parentBoundary);
        if (current)
            ++depth;
    }
    return depth;
}

// Graphviz guarantees geometric membership; the board may reserve a little
// more inside a frame (its title): the containment rule of dragging runs on
// the final positions, the board itself left where it was.
void containTargets(Board &board, Targets &targets)
{
    std::vector<std::pair<Shape *, QPointF>> starts;
    for (const auto &s : board.shapes())
        starts.emplace_back(s.get(), QPointF(s->x, s->y));
    targets.apply();
    std::vector<Shape *> contained;
    for (const auto &s : board.shapes()) {
        if (s->staysInsideParent() && !s->parentBoundary.isEmpty())
            contained.push_back(s.get());
    }
    std::stable_sort(contained.begin(), contained.end(), [&board](const Shape *a, const Shape *b) {
        return nestingDepth(board, a) < nestingDepth(board, b);
    });
    for (Shape *s : contained)
        board.containShape(*s);
    targets.retake();
    for (const auto &[s, p] : starts) {
        s->x = p.x();
        s->y = p.y();
    }
}

// Dot's edges (pos) and the places it reserved for their labels (lp),
// anchored at the final positions.
class CurveReader {
public:
    CurveReader(const Board &board, const LaidOutLinks &links, const ClusterSet &clusters, QStringList names)
        : board_(board)
        , links_(links)
        , clusters_(clusters)
        , names_(std::move(names))
    {
    }

    // twopi (mind maps) keeps its organic arcs.
    void apply(const QJsonArray &edges, qreal height, bool keepsArcs) const
    {
        for (qsizetype i = 0; i < edges.size(); ++i) {
            const QJsonObject edge = edges[i].toObject();
            Link *l = matchedLink(edge, i);
            if (l)
                applyEdge(*l, edge, height, keepsArcs);
        }
    }

private:
    Link *matchedLink(const QJsonObject &edge, qsizetype index) const
    {
        Link *l = links_.byId.value(edge.value(QStringLiteral("id")).toString());
        if (!l && size_t(index) < links_.inOrder.size())
            l = links_.inOrder[size_t(index)];
        if (!l || !joinsExpectedEnds(edge, links_.endpointsById.value(l->id)))
            return nullptr;
        return l;
    }

    bool joinsExpectedEnds(const QJsonObject &edge, const std::pair<QString, QString> &expected) const
    {
        const int tail = edge.value(QStringLiteral("tail")).toInt(-1);
        const int head = edge.value(QStringLiteral("head")).toInt(-1);
        const bool isInRange = tail >= 0 && head >= 0 && tail < names_.size() && head < names_.size();
        if (!isInRange)
            return false;
        return names_[tail] == expected.first && names_[head] == expected.second;
    }

    void applyEdge(Link &l, const QJsonObject &edge, qreal height, bool keepsArcs) const
    {
        l.spline.reset();
        const Shape *a = board_.shape(l.from);
        const Shape *b = board_.shape(l.to);
        const std::vector<qreal> lp = numbers(edge.value(QStringLiteral("lp")).toString());
        std::optional<QPointF> labelPoint;
        if (lp.size() == 2)
            labelPoint = QPointF(lp[0], height - lp[1]);
        // A relation touching a frame was routed to its invisible anchor: the
        // board draws another line, so the reserved label place is dropped
        // with the curve.
        const bool touchesCluster = clusters_.byId.contains(l.from) || clusters_.byId.contains(l.to);
        const bool hasEnds = a && b;
        if (labelPoint && hasEnds && !touchesCluster)
            l.layoutLabelPoint = Link::ReservedLabel{*labelPoint, QPointF(a->x, a->y), QPointF(b->x, b->y)};
        else
            l.layoutLabelPoint.reset();
        // Self-loops keep their loop.
        const bool takesCurve = !keepsArcs && !touchesCluster && l.from != l.to && hasEnds;
        if (!takesCurve)
            return;
        std::optional<Spline> parsed = parseEdgePos(edge.value(QStringLiteral("pos")).toString(), height);
        if (!parsed)
            return;
        parsed->labelPoint = labelPoint;
        parsed->fromAnchor = QPointF(a->x, a->y);
        parsed->toAnchor = QPointF(b->x, b->y);
        l.spline = std::move(parsed);
    }

    const Board &board_;
    const LaidOutLinks &links_;
    const ClusterSet &clusters_;
    QStringList names_;
};

QHash<QString, QSizeF> layoutSizes(const Board &board)
{
    QHash<QString, QSizeF> sizes;
    for (const auto &s : board.shapes())
        sizes.insert(s->id, layoutSize(*s));
    return sizes;
}
} // namespace

bool autoArrange(Board &board, const ArrangeOptions &options, QString *error)
{
    if (board.isEmpty())
        return true;
    // Shapes size themselves to their text in measure(): Graphviz gets the
    // real sizes.
    board.measureAll();
    const QByteArray base = board.snapshot();
    ArrangeOptions settled = options;
    if (settled.rankdir.isEmpty())
        settled.rankdir = QStringLiteral("TB");
    if (settled.engine.isEmpty())
        settled.engine = QStringLiteral("dot");
    const QString &rankdir = settled.rankdir;
    const QString &engine = settled.engine;
    const QHash<QString, QSizeF> sizes = layoutSizes(board);
    const ArrangeDot arrangeDot = writeArrangeDot(board, settled, sizes);
    const QString &dot = arrangeDot.text;
    const ClusterSet &clusters = arrangeDot.clusters;
    const LaidOutLinks &links = arrangeDot.links;

    std::optional<QJsonObject> laidOut = runLayout(dot, engine, error);
    if (!laidOut)
        return false;
    const bool hasOtherDirection = rankdir == QLatin1String("LR") || rankdir == QLatin1String("TB");
    if (options.adaptiveDirection && engine == QLatin1String("dot") && hasOtherDirection) {
        const Style &style = board.style();
        laidOut = betterFramed(*laidOut, dot, rankdir, {options.viewSize, std::min(style.maxScale, style.fitMaxScale)});
    }
    const QJsonObject &g = *laidOut;
    const std::vector<qreal> bb = boundingBox(g);
    if (bb.size() != 4) {
        if (error)
            *error = QStringLiteral("Graphviz returned no drawing");
        return false;
    }
    const qreal height = bb[3] - bb[1];
    const QJsonArray objects = g.value(QStringLiteral("objects")).toArray();
    Targets targets = readTargets(board, objects, sizes, {height, options.clusters});
    containTargets(board, targets);
    targets.apply();
    const CurveReader curves(board, links, clusters, objectNames(objects));
    curves.apply(g.value(QStringLiteral("edges")).toArray(), height, engine == QLatin1String("twopi"));
    board.resolveLabels();
    board.pushHistory(base);
    board.touch();
    return true;
}

} // namespace diagram::graphviz
