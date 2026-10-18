// The transitions of a state diagram: their routes (parallel transitions
// fan out, a self-transition loops), and the placement of their labels away
// from the states and from each other.

#include "States.h"
#include "viewers/diagram/Board.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace diagram::states {

// ---------------------------------------------------------------------------
// TransitionLink

namespace {

qreal labelHeight(const Board &board, const Link &link)
{
    if (link.label.isEmpty())
        return 24;
    const auto &o = board.style().linkLabel;
    return link.label.split(QLatin1Char('\n')).size() * (o.fontPx + 2) + o.padY * 2;
}

// The ends of a self-loop and its quadratic's control point, beyond the
// shape's top-right corner.
struct Loop {
    QPointF a;
    QPointF b;
    QPointF control;
};

Loop selfLoop(const Shape &s, qreal radius)
{
    const QPointF a(s.x + s.w * 0.75, s.y);
    const QPointF b(s.x + s.w, s.y + s.h * 0.25);
    return {a, b, QPointF(b.x() + radius, a.y() - radius)};
}

QPointF quadraticPoint(QPointF p0, QPointF p1, QPointF p2, qreal t)
{
    const qreal u = 1 - t;
    return u * u * p0 + 2 * u * t * p1 + t * t * p2;
}

} // namespace

TransitionLink::TransitionLink()
    : Link(QStringLiteral("transition"))
{
}

qreal TransitionLink::parallelSpread(const Board &board) const
{
    // The generic 12 px tell two strokes apart, not two pills about 24 px
    // high: after a manual move (which drops the layout's curves) they
    // overlapped.
    return Link::parallelSpread(board) * 2.75;
}

qreal TransitionLink::selfOffset(const Board &board) const
{
    // The loops of the same state before this one, each separated by the
    // real heights of the two neighboring pills (multi-line labels too).
    std::vector<const Link *> siblings;
    for (const auto &l : board.links()) {
        if (dynamic_cast<const TransitionLink *>(l.get()) && l->from == from && l->to == to)
            siblings.push_back(l.get());
    }
    const auto it = std::find(siblings.begin(), siblings.end(), this);
    const qsizetype index = it == siblings.end() ? -1 : it - siblings.begin();
    qreal offset = 0;
    for (qsizetype i = 1; i <= index; ++i)
        offset += (labelHeight(board, *siblings[i - 1]) + labelHeight(board, *siblings[i])) / 2 + 8;
    return offset;
}

std::optional<Route> TransitionLink::route(const Board &board) const
{
    if (from != to)
        return Link::route(board);
    const Shape *s = board.shape(from);
    if (!s)
        return std::nullopt;
    const Loop loop = selfLoop(*s, 26 + selfOffset(board));
    // The loop as a polyline: what hit-testing and the bounds measure. It
    // is drawn as the quadratic itself.
    Route r;
    r.a = loop.a;
    r.b = loop.b;
    for (int i = 0; i <= 16; ++i)
        r.points.push_back(quadraticPoint(loop.a, loop.control, loop.b, i / 16.0));
    return r;
}

std::optional<QPointF> TransitionLink::validPlacement(const Board &board) const
{
    if (!placement)
        return std::nullopt;
    const Shape *a = board.shape(from);
    const Shape *b = board.shape(to);
    if (!a || !b || std::abs(a->x - placement->fromAnchor.x()) > 0.5 ||
        std::abs(a->y - placement->fromAnchor.y()) > 0.5 || std::abs(b->x - placement->toAnchor.x()) > 0.5 ||
        std::abs(b->y - placement->toAnchor.y()) > 0.5) {
        placement.reset();
        return std::nullopt;
    }
    return placement->point;
}

QPointF TransitionLink::labelPos(const Route &r, const Board &board) const
{
    if (const std::optional<QPointF> placed = validPlacement(board))
        return *placed;
    if (from == to) {
        const qreal offset = selfOffset(board);
        return {r.b.x() + 34 + offset * 0.25, r.a.y() - 20 - offset};
    }
    return Link::labelPos(r, board);
}

void TransitionLink::draw(QPainter &painter, const Board &board, bool selected) const
{
    if (from != to) {
        Link::draw(painter, board, selected); // straight, or the layout's curve
        return;
    }
    const Shape *s = board.shape(from);
    if (!s)
        return;
    const LinkStyle style = drawStyle(board, selected);
    // A quadratic arc from the top edge, around the corner, to the right
    // edge; the head arrives along the arc's last tangent.
    const Loop loop = selfLoop(*s, 26 + selfOffset(board));
    const qreal angle = std::atan2(loop.b.y() - loop.control.y(), loop.b.x() - loop.control.x());
    const qreal inset = arrowBaseInset(style.arrow);
    QPainterPath path;
    path.moveTo(loop.a);
    path.quadTo(loop.control, QPointF(loop.b.x() - inset * std::cos(angle), loop.b.y() - inset * std::sin(angle)));
    stroke(painter, path, style.color, style.width, linkDash(*this));
    fillArrowHead(painter, loop.b, angle, style.arrow, style.color);
}

// ---------------------------------------------------------------------------
// Label resolution

namespace {

qreal overlapArea(const QRectF &a, const QRectF &b)
{
    return std::max<qreal>(0, std::min(a.right(), b.right()) - std::max(a.left(), b.left())) *
           std::max<qreal>(0, std::min(a.bottom(), b.bottom()) - std::max(a.top(), b.top()));
}

std::optional<QPointF> pointOnPolyline(const std::vector<QPointF> &points, qreal fraction)
{
    if (points.empty())
        return std::nullopt;
    if (points.size() == 1)
        return points.front();
    std::vector<qreal> lengths;
    qreal total = 0;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const qreal length = std::hypot(points[i + 1].x() - points[i].x(), points[i + 1].y() - points[i].y());
        lengths.push_back(length);
        total += length;
    }
    qreal remaining = total * fraction;
    for (size_t i = 0; i < lengths.size(); ++i) {
        if (remaining <= lengths[i] && lengths[i] > 0) {
            const qreal ratio = remaining / lengths[i];
            return points[i] + (points[i + 1] - points[i]) * ratio;
        }
        remaining -= lengths[i];
    }
    return points.back();
}

// A label's pill with a margin, centered on `point`.
QRectF paddedBoxAt(QPointF point, const Link::LabelBox &size)
{
    return {point.x() - size.width / 2 - 5, point.y() - size.height / 2 - 5, size.width + 10, size.height + 10};
}

// What a label should avoid: the states (with a margin) and the labels
// already placed, which weigh twice as much.
struct LabelObstacles {
    std::vector<QRectF> shapes;
    std::vector<QRectF> labels;

    qreal collision(const QRectF &box) const
    {
        qreal area = 0;
        for (const QRectF &shape : shapes)
            area += overlapArea(box, shape);
        for (const QRectF &label : labels)
            area += overlapArea(box, label) * 2;
        return area;
    }
};

struct LabelCandidate {
    qreal fraction = 0.5; // where along the route, 0.5 being its middle
    QPointF point;
};

struct LabelChoice {
    QPointF point;
    QRectF box;
};

std::vector<TransitionLink *> labelledTransitions(const Board &board)
{
    std::vector<TransitionLink *> links;
    for (const auto &l : board.links()) {
        if (auto *t = dynamic_cast<TransitionLink *>(l.get()); t && !t->label.isEmpty())
            links.push_back(t);
    }
    return links;
}

// The candidates all lie ON the route, from its middle outwards; a self-loop
// keeps its own place.
std::vector<LabelCandidate> labelCandidates(const TransitionLink &link, const Route &route, const Board &board)
{
    std::vector<LabelCandidate> candidates;
    if (link.from != link.to) {
        const std::vector<QPointF> points = link.routePolyline(route);
        for (const qreal fraction : {0.5, 0.4, 0.6, 0.3, 0.7, 0.2, 0.8, 0.12, 0.88}) {
            if (const std::optional<QPointF> p = pointOnPolyline(points, fraction))
                candidates.push_back({fraction, *p});
        }
    }
    if (candidates.empty())
        candidates.push_back({0.5, link.labelPos(route, board)});
    return candidates;
}

// The candidate that overlaps least, the closest to the middle among equals.
std::optional<LabelChoice> leastColliding(const std::vector<LabelCandidate> &candidates,
                                          const Link::LabelBox &size,
                                          const LabelObstacles &obstacles)
{
    std::optional<LabelChoice> best;
    qreal bestScore = 0;
    for (const LabelCandidate &candidate : candidates) {
        const QRectF box = paddedBoxAt(candidate.point, size);
        const qreal score = obstacles.collision(box) * 10000 + std::abs(candidate.fraction - 0.5) * 100;
        if (!best || score < bestScore) {
            best = LabelChoice{candidate.point, box};
            bestScore = score;
        }
    }
    return best;
}

void placeLabel(TransitionLink &link, const Board &board, LabelObstacles &obstacles)
{
    const std::optional<Route> r = link.route(board);
    if (!r)
        return;
    const Link::LabelBox size = link.labelBox(board);
    // After a layout, dot placed every label together and avoided their
    // collisions: that result is kept. This pass is for after a manual
    // move, when the curve and its `lp` no longer hold.
    if (r->labelPoint) {
        obstacles.labels.push_back(paddedBoxAt(*r->labelPoint, size));
        return;
    }
    const std::optional<LabelChoice> best = leastColliding(labelCandidates(link, *r, board), size, obstacles);
    const Shape *a = board.shape(link.from);
    const Shape *b = board.shape(link.to);
    if (!best || !a || !b)
        return;
    link.placement = TransitionLink::Placement{best->point, QPointF(a->x, a->y), QPointF(b->x, b->y)};
    obstacles.labels.push_back(best->box);
}

} // namespace

void resolveLabels(const Board &board)
{
    const std::vector<TransitionLink *> links = labelledTransitions(board);
    for (TransitionLink *link : links)
        link->placement.reset();

    LabelObstacles obstacles;
    for (const auto &s : board.shapes())
        obstacles.shapes.push_back(s->rect().adjusted(-9, -9, 9, 9));
    // Graphviz avoids its own text boxes, but the board paints wider pills
    // (padding, border, the real font): on a dense fan two distinct `lp` can
    // still overlap.
    for (TransitionLink *link : links)
        placeLabel(*link, board, obstacles);
}

} // namespace diagram::states
