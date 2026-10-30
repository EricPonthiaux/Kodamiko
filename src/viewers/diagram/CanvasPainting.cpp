// SPDX-License-Identifier: GPL-3.0-only

// The painting of the canvas: the scene, its animations and its decorations.
#include "Board.h"
#include "Canvas.h"
#include "CanvasMotion.h"
#include "Family.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <algorithm>
#include <cmath>

namespace diagram {

namespace {

using motion::clamp;
using motion::easeOut;

QRectF lerp(const QRectF &a, const QRectF &b, qreal t)
{
    return {a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t, a.width() + (b.width() - a.width()) * t,
            a.height() + (b.height() - a.height()) * t};
}

enum class ArrowHead {
    Shown,
    Hidden
};

struct ArrowPreview {
    QPointF from;
    QPointF to;
    QList<qreal> dash;
    ArrowHead head = ArrowHead::Shown;
};

void drawArrowPreview(QPainter &painter, const ArrowPreview &arrow, const Style &style)
{
    const QPointF a = arrow.from;
    const QPointF b = arrow.to;
    const qreal angle = std::atan2(b.y() - a.y(), b.x() - a.x());
    const bool hasHead = arrow.head == ArrowHead::Shown;
    const qreal inset = hasHead ? arrowBaseInset(style.link.arrow) : 0;
    strokeLine(painter, a, QPointF(b.x() - inset * std::cos(angle), b.y() - inset * std::sin(angle)), style.link.color,
               style.link.width, arrow.dash);
    if (hasHead)
        fillArrowHead(painter, b, angle, style.link.arrow, style.link.color);
}

QList<qreal> dashPattern(const QString &line)
{
    if (line == QLatin1String("dashed"))
        return {8, 5};
    if (line == QLatin1String("dotted"))
        return {2, 5};
    return {};
}

QPainterPath polylinePath(const std::vector<QPointF> &points)
{
    QPainterPath path;
    path.moveTo(points.front());
    for (size_t i = 1; i < points.size(); ++i)
        path.lineTo(points[i]);
    return path;
}

// How many frames hold `shape`, to draw parents before their children.
int nestingDepth(const Board &board, const Shape &shape)
{
    int depth = 0;
    const Shape *current = &shape;
    QSet<const Shape *> seen{current};
    while (!current->parentBoundary.isEmpty()) {
        current = board.shapeByReference(current->parentBoundary);
        if (!current || seen.contains(current))
            break;
        seen.insert(current);
        ++depth;
    }
    return depth;
}

// A shape just created grows and fades in around its center.
void scaleIn(QPainter &painter, const Shape &shape, qreal progress)
{
    painter.setOpacity(progress);
    const qreal scale = 0.96 + 0.04 * progress;
    painter.translate(shape.cx(), shape.cy());
    painter.scale(scale, scale);
    painter.translate(-shape.cx(), -shape.cy());
}

// ---------------------------------------------------------------------------
// The geometry shown while shapes slide to their new places

// A shape moved to where the animation shows it, with its true box.
struct SavedShape {
    Shape *shape{};
    QRectF box;
};

// A link warped for the frame, with its true curve.
struct SavedLink {
    Link *link{};
    std::optional<Spline> spline;
    std::optional<Link::ReservedLabel> label;
};

std::vector<SavedShape> moveShapesToView(const Board &board,
                                         const QHash<QString, QRectF> &from,
                                         const QHash<QString, QRectF> &to,
                                         qreal t)
{
    std::vector<SavedShape> moved;
    for (const auto &s : board.shapes()) {
        const auto start = from.constFind(s->id);
        const auto end = to.constFind(s->id);
        if (start == from.cend() || end == to.cend())
            continue;
        moved.push_back({s.get(), s->rect()});
        const QRectF view = lerp(*start, *end, t);
        s->x = view.x();
        s->y = view.y();
        s->w = view.width();
        s->h = view.height();
    }
    return moved;
}

QRectF finalBox(const std::vector<SavedShape> &moved, const Shape &shape)
{
    const auto it =
        std::find_if(moved.begin(), moved.end(), [&shape](const SavedShape &s) { return s.shape == &shape; });
    return it != moved.end() ? it->box : shape.rect();
}

// How far a link's end moves with its shape: the shape's offset, plus its
// scaling around the anchor.
QPointF endShift(const Shape &shape, const QRectF &finalBox, QPointF anchor)
{
    const qreal finalWidth = finalBox.width() != 0 ? finalBox.width() : 1;
    const qreal finalHeight = finalBox.height() != 0 ? finalBox.height() : 1;
    return {shape.x - finalBox.x() + (anchor.x() - finalBox.x()) * (shape.w / finalWidth - 1),
            shape.y - finalBox.y() + (anchor.y() - finalBox.y()) * (shape.h / finalHeight - 1)};
}

// The shifts of a curve's two ends; a point between them moves by a blend
// of both, so that no arrow ever detaches from its node.
struct EndShifts {
    QPointF a;
    QPointF b;

    // `u` runs from 0 at the first end to 1 at the second.
    QPointF apply(QPointF point, qreal u) const
    {
        return {point.x() + a.x() * (1 - u) + b.x() * u, point.y() + a.y() * (1 - u) + b.y() * u};
    }
};

EndShifts endShifts(const Spline &spline, const Shape &a, const Shape &b, const std::vector<SavedShape> &moved)
{
    const QPointF anchorA = spline.start.value_or(spline.points.front());
    const QPointF anchorB = spline.end.value_or(spline.points.back());
    return {endShift(a, finalBox(moved, a), anchorA), endShift(b, finalBox(moved, b), anchorB)};
}

// The final curve warped with its two ends, morphed from the previous
// curve when that one had the same number of points.
Spline warpedSpline(Spline spline, const EndShifts &shifts, const Spline *previous, qreal t)
{
    const size_t n = spline.points.size();
    for (size_t i = 0; i < n; ++i)
        spline.points[i] = shifts.apply(spline.points[i], qreal(i) / std::max<size_t>(1, n - 1));
    if (previous && previous->points.size() == n) {
        for (size_t i = 1; i + 1 < n; ++i)
            spline.points[i] = previous->points[i] * (1 - t) + spline.points[i] * t;
    }
    if (spline.start)
        spline.start = shifts.apply(*spline.start, 0);
    if (spline.end)
        spline.end = shifts.apply(*spline.end, 1);
    if (spline.labelPoint)
        spline.labelPoint = shifts.apply(*spline.labelPoint, 0.5);
    return spline;
}

std::vector<SavedLink> warpLinks(const Board &board,
                                 const std::vector<SavedShape> &moved,
                                 const QHash<QString, Spline> &previousSplines,
                                 qreal t)
{
    std::vector<SavedLink> saved;
    for (const auto &l : board.links()) {
        saved.push_back({l.get(), l->spline, l->layoutLabelPoint});
        if (!l->spline)
            continue;
        const Shape *a = board.shape(l->from);
        const Shape *b = board.shape(l->to);
        if (!a || !b)
            continue;
        const auto previous = previousSplines.constFind(l->id);
        const Spline *previousSpline = previous != previousSplines.cend() ? &*previous : nullptr;
        const EndShifts shifts = endShifts(*l->spline, *a, *b, moved);
        Spline spline = warpedSpline(*l->spline, shifts, previousSpline, t);
        spline.fromAnchor = QPointF(a->x, a->y);
        spline.toAnchor = QPointF(b->x, b->y);
        l->spline = spline;
        if (const std::optional<Link::ReservedLabel> label = l->layoutLabelPoint)
            l->layoutLabelPoint =
                Link::ReservedLabel{shifts.apply(label->point, 0.5), QPointF(a->x, a->y), QPointF(b->x, b->y)};
    }
    return saved;
}

void restoreShapes(const std::vector<SavedShape> &saved)
{
    for (const SavedShape &s : saved) {
        s.shape->x = s.box.x();
        s.shape->y = s.box.y();
        s.shape->w = s.box.width();
        s.shape->h = s.box.height();
    }
}

void restoreLinks(const std::vector<SavedLink> &saved)
{
    for (const SavedLink &l : saved) {
        l.link->spline = l.spline;
        l.link->layoutLabelPoint = l.label;
    }
}

} // namespace

bool Canvas::portsVisible(const Shape &shape) const
{
    return board_->selection().contains(shape.id) || shape.showConnectionPorts() ||
           (shape.showConnectionPortsOnHover() && hoverConnectionShape_ == shape.id);
}

void Canvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    render(painter, rect());
}

void Canvas::drawGrid(QPainter &painter, const QRectF &area) const
{
    // A dot grid: it hints at alignment without ever squaring the drawing,
    // and vanishes when zoomed out.
    const qreal step = 32 * camera_.scale;
    if (step < 12)
        return;
    const qreal ox = std::fmod(camera_.x, step);
    const qreal oy = std::fmod(camera_.y, step);
    const qreal r = clamp(step * 0.045, 0.8, 1.6);
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(board_->style().gridColor);
    for (int column = 0; ox + column * step < area.right(); ++column) {
        const qreal x = ox + column * step;
        for (int row = 0; oy + row * step < area.bottom(); ++row)
            painter.drawEllipse(QPointF(x, oy + row * step), r, r);
    }
    painter.restore();
}

template <typename Fn>
void Canvas::withVisualGeometry(Fn draw)
{
    if (geometryStart_ < 0 || geometryFrom_.isEmpty()) {
        draw();
        return;
    }
    // The model is at its destination; it is moved back for the frame only.
    const qreal t = easeOut((clock_.elapsed() - geometryStart_) / motion::geometryMs);
    const std::vector<SavedShape> shapes = moveShapesToView(*board_, geometryFrom_, geometryTo_, t);
    const std::vector<SavedLink> links = warpLinks(*board_, shapes, splinesFrom_, t);
    draw();
    restoreShapes(shapes);
    restoreLinks(links);
}

void Canvas::render(QPainter &painter, const QRectF &area)
{
    const Style &style = board_->style();
    painter.fillRect(area, style.background);
    if (gridVisible_)
        drawGrid(painter, area);
    if (board_->isEmpty() && ghosts_.empty()) {
        text::draw(painter, Font(13), tr("Double-click or right-click to add an element."), area.center(),
                   withAlpha(style.palette.ink, 0.45), Align::Center, Baseline::Middle);
        return;
    }
    painter.save();
    painter.translate(camera_.x, camera_.y);
    painter.scale(camera_.scale, camera_.scale);
    withVisualGeometry([&] { drawScene(painter); });
    painter.restore();
    if (drag_ == Drag::Rubber)
        drawRubberBand(painter);
}

void Canvas::drawScene(QPainter &painter) const
{
    const qreal now = clock_.elapsed();
    const Layers layered = layers();
    if (const Family *family = board_->family(); family && family->drawUnderlay)
        family->drawUnderlay(painter, *board_);
    for (const Shape *s : layered.background)
        drawShape(painter, *s, now);
    for (const auto &l : board_->links())
        l->draw(painter, *board_, board_->linkSelection().contains(l->id));
    drawConnectPreview(painter);
    drawTracePreview(painter);
    for (const Shape *s : layered.foreground) {
        drawShape(painter, *s, now);
        drawHandles(painter, *s);
    }
    drawGhosts(painter, now);
    drawGuides(painter);
    // A selected container's handles stay above its relations.
    for (const Shape *s : layered.background)
        drawHandles(painter, *s);
    drawLinkOverlays(painter);
}

Canvas::Layers Canvas::layers() const
{
    Layers result;
    for (const auto &s : board_->shapes())
        (s->drawBehindLinks() ? result.background : result.foreground).push_back(s.get());
    // Containers first, ordered parents before children and larger
    // before smaller: they never hide a relation.
    const Board &board = *board_;
    std::stable_sort(result.background.begin(), result.background.end(), [&board](const Shape *a, const Shape *b) {
        const int da = nestingDepth(board, *a);
        const int db = nestingDepth(board, *b);
        return da != db ? da < db : a->w * a->h > b->w * b->h;
    });
    return result;
}

void Canvas::drawShape(QPainter &painter, const Shape &shape, qreal now) const
{
    const auto appear = appearing_.constFind(shape.id);
    const bool isAppearing = appear != appearing_.cend();
    if (isAppearing) {
        painter.save();
        scaleIn(painter, shape, easeOut((now - appear->start) / appear->duration));
    }
    ShapeState state;
    state.selected = board_->selection().contains(shape.id);
    state.editing = shape.id == editShape_ && isEditingText();
    state.dropTarget = shape.id == dropTarget_;
    shape.draw(painter, *board_, state);
    if (isAppearing)
        painter.restore();
    if (state.dropTarget) {
        const Style &style = board_->style();
        const QPainterPath halo = roundRect(shape.x - 3, shape.y - 3, shape.w + 6, shape.h + 6, style.shape.radius + 3);
        stroke(painter, halo, style.shape.selectedStroke, 2 / camera_.scale, {6 / camera_.scale, 4 / camera_.scale});
    }
}

void Canvas::drawHandles(QPainter &painter, const Shape &shape) const
{
    const bool selected = board_->selection().contains(shape.id);
    if (selected)
        drawSelectionHalo(painter, shape);
    if (portsVisible(shape))
        drawPorts(painter, shape);
    if (selected && shape.resizable())
        drawResizeHandle(painter, shape);
}

void Canvas::drawConnectPreview(QPainter &painter) const
{
    if (drag_ != Drag::Connect)
        return;
    const QPointF start(dragPort_.x, dragPort_.y);
    QPointF end = pointer_;
    const Shape *from = board_->shape(dragShape_);
    const Shape *target = connectionShapeAt(pointer_);
    if (target && target != from)
        end = target->connectionPoint(*board_, start, pointer_);
    drawArrowPreview(painter, {.from = start, .to = end}, board_->style());
}

void Canvas::drawTracePreview(QPainter &painter) const
{
    if (drag_ != Drag::Trace || !trace_)
        return;
    const QString arrow = trace_->props.value(QStringLiteral("arrow")).toString(QStringLiteral("none"));
    const QString line = trace_->props.value(QStringLiteral("line")).toString(QStringLiteral("solid"));
    const Style &style = board_->style();
    if (arrow == QLatin1String("start")) {
        drawArrowPreview(painter, {.from = pointer_, .to = dragStart_, .dash = dashPattern(line)}, style);
        return;
    }
    const bool hasHead = arrow == QLatin1String("end") || arrow == QLatin1String("both");
    drawArrowPreview(painter,
                     {.from = dragStart_,
                      .to = pointer_,
                      .dash = dashPattern(line),
                      .head = hasHead ? ArrowHead::Shown : ArrowHead::Hidden},
                     style);
}

void Canvas::drawGhosts(QPainter &painter, qreal now) const
{
    const Style &style = board_->style();
    for (const Ghost &ghost : ghosts_) {
        painter.save();
        painter.setOpacity(1 - clamp((now - ghost.start) / motion::ghostMs, 0, 1));
        if (ghost.shape)
            ghost.shape->draw(painter, *board_, ShapeState{});
        else if (ghost.line.size() > 1)
            stroke(painter, polylinePath(ghost.line), ghost.color, style.link.width);
        painter.restore();
    }
}

void Canvas::drawGuides(QPainter &painter) const
{
    // A guide points at an alignment, it does not designate anything: in
    // the guide color rather than the selection's.
    const Style &style = board_->style();
    const qreal pad = 16 / camera_.scale;
    for (const Guide &g : guides_) {
        const QPointF a = g.vertical ? QPointF(g.value, g.from - pad) : QPointF(g.from - pad, g.value);
        const QPointF b = g.vertical ? QPointF(g.value, g.to + pad) : QPointF(g.to + pad, g.value);
        strokeLine(painter, a, b, style.shape.guide, 1 / camera_.scale, {5 / camera_.scale, 4 / camera_.scale});
    }
}

void Canvas::drawLinkOverlays(QPainter &painter) const
{
    if (const Family *family = board_->family(); family && family->drawOverlay)
        family->drawOverlay(painter, *board_);
    // Labels and selected endpoints last, above the shapes they cross.
    for (const auto &l : board_->links()) {
        l->drawLabel(painter, *board_, l->id == editLink_ && isEditingText());
        if (board_->linkSelection().contains(l->id))
            l->drawEndpoints(painter, *board_);
    }
}

void Canvas::drawRubberBand(QPainter &painter) const
{
    const Style &style = board_->style();
    const QRectF band = QRectF(dragStartScreen_, camera_.toScreen(pointer_)).normalized();
    painter.save();
    painter.setPen(QPen(style.shape.selectedStroke, 1));
    painter.setBrush(withAlpha(style.shape.selectedStroke, 0.08));
    painter.drawRect(band);
    painter.restore();
}

void Canvas::drawSelectionHalo(QPainter &painter, const Shape &shape) const
{
    const Style &style = board_->style();
    const qreal grow = 4;
    painter.save();
    painter.setOpacity(0.34);
    stroke(painter,
           roundRect(shape.x - grow, shape.y - grow, shape.w + grow * 2, shape.h + grow * 2,
                     std::max<qreal>(5, style.shape.radius + 2)),
           style.shape.selectedStroke, 1.25);
    painter.restore();
}

void Canvas::drawResizeHandle(QPainter &painter, const Shape &shape) const
{
    const Style &style = board_->style();
    const qreal size = 10 / std::max(0.01, camera_.scale);
    const QRectF handle(shape.x + shape.w - size / 2, shape.y + shape.h - size / 2, size, size);
    QPainterPath path;
    path.addRect(handle);
    painter.fillPath(path, style.port.fill);
    stroke(painter, path, style.shape.selectedStroke, 1.5 / std::max(0.01, camera_.scale));
}

void Canvas::drawPorts(QPainter &painter, const Shape &shape) const
{
    const Style &style = board_->style();
    for (const Port &port : shape.connectionPorts(*board_)) {
        const QPainterPath dot = ellipse(QPointF(port.x, port.y), style.port.radius, style.port.radius);
        painter.fillPath(dot.translated(0, 1), withAlpha(style.palette.shadow, style.palette.shadow.alphaF()));
        painter.fillPath(dot, style.port.fill);
        stroke(painter, dot, style.port.stroke, 1.5);
    }
}

void Canvas::paintBoard(QPainter &painter, const Board &board)
{
    std::vector<const Shape *> background;
    std::vector<const Shape *> foreground;
    for (const auto &s : board.shapes())
        (s->drawBehindLinks() ? background : foreground).push_back(s.get());
    std::stable_sort(background.begin(), background.end(),
                     [](const Shape *a, const Shape *b) { return a->w * a->h > b->w * b->h; });
    if (const Family *family = board.family(); family && family->drawUnderlay)
        family->drawUnderlay(painter, board);
    for (const Shape *s : background)
        s->draw(painter, board, ShapeState{});
    for (const auto &l : board.links())
        l->draw(painter, board, false);
    for (const Shape *s : foreground)
        s->draw(painter, board, ShapeState{});
    if (const Family *family = board.family(); family && family->drawOverlay)
        family->drawOverlay(painter, board);
    for (const auto &l : board.links())
        l->drawLabel(painter, board, false);
}

QIcon Canvas::presetIcon(const Family &family, const ElementPreset &preset, const Style &style)
{
    Board board;
    board.setFamily(&family);
    board.setStyle(style);
    const Shape *s = board.addShape(preset.shapeType, preset.props);
    QPixmap pixmap(QSize(40, 28) * 2);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    if (s) {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = s->bounds(board).value_or(s->rect());
        const qreal scale = std::min(36 / std::max<qreal>(1, box.width()), 24 / std::max<qreal>(1, box.height()));
        painter.translate(20, 14);
        painter.scale(scale, scale);
        painter.translate(-box.center());
        s->draw(painter, board, ShapeState{});
    }
    return {pixmap};
}

QIcon Canvas::relationIcon(const Family &family, const RelationPreset &preset, const Style &style)
{
    Board board;
    board.setFamily(&family);
    board.setStyle(style);
    Shape *a = board.addShape(family.defaultShape, {});
    Shape *b = board.addShape(family.defaultShape, {});
    QPixmap pixmap(QSize(40, 28) * 2);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    if (a && b) {
        a->x = 0;
        a->y = 0;
        a->w = a->h = 10;
        b->x = 110;
        b->y = 0;
        b->w = b->h = 10;
        QJsonObject props = preset.props;
        props.remove(QStringLiteral("label"));
        if (Link *l = board.addLink(family.defaultLink, a->id, b->id, props)) {
            l->label.clear();
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.translate(2, 14);
            painter.scale(36.0 / 110, 36.0 / 110);
            painter.translate(-5, -5);
            l->draw(painter, board, false);
        }
    }
    return {pixmap};
}

} // namespace diagram
