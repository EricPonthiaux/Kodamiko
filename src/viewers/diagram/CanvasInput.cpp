// SPDX-License-Identifier: GPL-3.0-only

// The pointer and keyboard interaction of the canvas: hit tests, gestures
// (pan, move, resize, connect, rubber band, trace) and shortcuts.
#include "Board.h"
#include "Canvas.h"
#include "Family.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>

namespace diagram {

namespace {

// Keys the canvas handles before the window's shortcuts (Ctrl+Z, Delete…).
bool isCanvasShortcut(const QKeyEvent *event)
{
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    const int key = event->key();
    if (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier) {
        switch (key) {
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
        case Qt::Key_Escape:
        case Qt::Key_F2:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_F:
        case Qt::Key_Space:
            return true;
        default:
            return false;
        }
    }
    if (modifiers == Qt::ControlModifier) {
        switch (key) {
        case Qt::Key_Z:
        case Qt::Key_Y:
        case Qt::Key_C:
        case Qt::Key_X:
        case Qt::Key_V:
        case Qt::Key_D:
        case Qt::Key_A:
        case Qt::Key_L:
        case Qt::Key_0:
        case Qt::Key_Equal:
        case Qt::Key_Plus:
        case Qt::Key_Minus:
            return true;
        default:
            return false;
        }
    }
    return modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && key == Qt::Key_Z;
}

// The displacement of an arrow key, `step` long.
QPointF arrowDelta(int key, qreal step)
{
    switch (key) {
    case Qt::Key_Left:
        return {-step, 0};
    case Qt::Key_Right:
        return {step, 0};
    case Qt::Key_Up:
        return {0, -step};
    case Qt::Key_Down:
        return {0, step};
    default:
        return {};
    }
}

// How a child names the frame that holds it.
QString containerReference(const Shape &container)
{
    return container.key.isEmpty() ? container.id : container.key;
}

// ---------------------------------------------------------------------------
// Alignment on the other shapes

using Edges = std::array<qreal, 3>;

Edges xEdges(const QRectF &rect)
{
    return {rect.left(), rect.center().x(), rect.right()};
}

Edges yEdges(const QRectF &rect)
{
    return {rect.top(), rect.center().y(), rect.bottom()};
}

// An edge or a center of `moving` brought onto one of a target's.
struct Alignment {
    qreal delta = 0;
    qreal value = 0;
    QRectF target;
};

// The smallest alignment within `threshold` along one axis; the first one
// found wins a tie.
std::optional<Alignment> closestAlignment(const QRectF &moving,
                                          const std::vector<QRectF> &targets,
                                          Edges (*edgesOf)(const QRectF &),
                                          qreal threshold)
{
    std::optional<Alignment> best;
    for (const QRectF &target : targets) {
        for (const qreal source : edgesOf(moving)) {
            for (const qreal value : edgesOf(target)) {
                const qreal delta = value - source;
                const bool isCloser = !best || std::abs(delta) < std::abs(best->delta);
                if (std::abs(delta) <= threshold && isCloser)
                    best = Alignment{delta, value, target};
            }
        }
    }
    return best;
}

// The smallest snapping of `moving` on the others' edges and centers.
struct Snap {
    qreal dx = 0;
    qreal dy = 0;
    std::vector<std::pair<bool, std::array<qreal, 3>>> guides; // vertical, value, from, to
};

Snap snapAlignment(const QRectF &moving, const std::vector<QRectF> &targets, qreal threshold)
{
    const std::optional<Alignment> vertical = closestAlignment(moving, targets, xEdges, threshold);
    const std::optional<Alignment> horizontal = closestAlignment(moving, targets, yEdges, threshold);
    Snap snap;
    snap.dx = vertical ? vertical->delta : 0;
    snap.dy = horizontal ? horizontal->delta : 0;
    const QRectF shifted = moving.translated(snap.dx, snap.dy);
    if (vertical) {
        const QRectF &t = vertical->target;
        snap.guides.push_back(
            {true, {vertical->value, std::min(shifted.top(), t.top()), std::max(shifted.bottom(), t.bottom())}});
    }
    if (horizontal) {
        const QRectF &t = horizontal->target;
        snap.guides.push_back(
            {false, {horizontal->value, std::min(shifted.left(), t.left()), std::max(shifted.right(), t.right())}});
    }
    return snap;
}

// The full extent (a stroke and its label) of the shapes dragged.
QRectF draggedBounds(const Board &board, const std::vector<std::pair<QString, QPointF>> &offsets)
{
    QRectF bounds;
    for (const auto &[id, offset] : offsets) {
        if (const Shape *s = board.shape(id)) {
            const QRectF box = s->bounds(board).value_or(s->rect());
            bounds = bounds.isNull() ? box : bounds.united(box);
        }
    }
    return bounds;
}

// Each other shape's full extent; the hidden ends of free arrows align
// nothing.
std::vector<QRectF> alignmentTargets(const Board &board, const QSet<QString> &moving)
{
    std::vector<QRectF> targets;
    for (const auto &s : board.shapes()) {
        if (!moving.contains(s->id) && s->type != QLatin1String("freeAnchor"))
            targets.push_back(s->bounds(board).value_or(s->rect()));
    }
    return targets;
}

} // namespace

// ---------------------------------------------------------------------------
// Hit tests

Shape *Canvas::shapeAt(QPointF point) const
{
    const auto &shapes = board_->shapes();
    for (const auto &shape : std::views::reverse(shapes)) {
        if (shape->hitTest(point))
            return shape.get();
    }
    return nullptr;
}

Shape *Canvas::connectionShapeAt(QPointF point) const
{
    const auto &shapes = board_->shapes();
    for (const auto &shape : std::views::reverse(shapes)) {
        if (shape->connectionHitTest(*board_, point))
            return shape.get();
    }
    return nullptr;
}

Link *Canvas::linkAt(QPointF point) const
{
    const qreal tolerance = 6 / camera_.scale;
    const auto &links = board_->links();
    for (const auto &link : std::views::reverse(links)) {
        const qreal width = link->customWidth.value_or(board_->style().link.width);
        if (link->hitTest(*board_, point, std::max(tolerance, width / 2)))
            return link.get();
    }
    return nullptr;
}

std::optional<std::pair<Shape *, Port>> Canvas::portAt(QPointF point) const
{
    const qreal r = (board_->style().port.radius + 4) / camera_.scale;
    for (const auto &s : board_->shapes()) {
        if (!portsVisible(*s))
            continue;
        for (const Port &port : s->connectionPorts(*board_)) {
            if (std::pow(point.x() - port.x, 2) + std::pow(point.y() - port.y, 2) <= r * r)
                return std::pair<Shape *, Port>{s.get(), port};
        }
    }
    return std::nullopt;
}

Shape *Canvas::resizeHandleAt(QPointF point) const
{
    const qreal radius = 10 / std::max(0.01, camera_.scale);
    for (const QString &id : board_->selection()) {
        Shape *s = board_->shape(id);
        if (!s || !s->resizable())
            continue;
        if (std::pow(point.x() - (s->x + s->w), 2) + std::pow(point.y() - (s->y + s->h), 2) <= radius * radius)
            return s;
    }
    return nullptr;
}

void Canvas::updateHover(QPointF screen)
{
    const QPointF p = world(screen);
    if (trace_) {
        setCursor(Qt::CrossCursor);
        return;
    }
    if (resizeHandleAt(p)) {
        setCursor(Qt::SizeFDiagCursor);
        return;
    }
    const auto port = portAt(p);
    const Shape *s = port ? port->first : shapeAt(p);
    if (!s)
        s = connectionShapeAt(p);
    const QString hover = s && s->showConnectionPortsOnHover() ? s->id : QString();
    if (hover != hoverConnectionShape_) {
        hoverConnectionShape_ = hover;
        update();
    }
    const bool isCodeLink = s && QGuiApplication::keyboardModifiers() == Qt::ControlModifier && s->codeReferenceAt(p);
    const bool isDiagramLink = !s && linkAt(p);
    if (portAt(p))
        setCursor(Qt::CrossCursor);
    else if (isCodeLink || isDiagramLink)
        setCursor(Qt::PointingHandCursor);
    else if (s)
        setCursor(Qt::SizeAllCursor);
    else
        setCursor(Qt::ArrowCursor);
}

// ---------------------------------------------------------------------------
// Pointer

void Canvas::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    if (isEditingText())
        endTextEdit(true);
    finishMotion();
    guides_.clear();
    const QPointF screen = event->position();
    const QPointF p = world(screen);
    pointer_ = p;
    dragStart_ = p;
    dragStartScreen_ = screen;
    moved_ = false;
    const Qt::MouseButton button = event->button();
    const bool isSpacePan = button == Qt::LeftButton && spaceDown_;
    if (button == Qt::MiddleButton || isSpacePan) {
        drag_ = Drag::Pan;
        panStart_ = camera_;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (button == Qt::RightButton) {
        drag_ = Drag::RightPending;
        panStart_ = camera_;
        return;
    }
    if (button == Qt::LeftButton)
        pressLeft(p, event->modifiers());
}

void Canvas::pressLeft(QPointF point, Qt::KeyboardModifiers modifiers)
{
    if (trace_) {
        drag_ = Drag::Trace;
        dragBase_ = board_->snapshot();
        return;
    }
    if (Shape *s = resizeHandleAt(point)) {
        startResize(*s);
        return;
    }
    if (const auto port = portAt(point)) {
        dragBase_ = board_->snapshot();
        drag_ = Drag::Connect;
        dragShape_ = port->first->id;
        dragPort_ = port->second;
        return;
    }
    if (modifiers == Qt::ControlModifier && requestCodeAt(point))
        return;
    const bool shift = modifiers & Qt::ShiftModifier;
    if (const Shape *s = shapeAt(point)) {
        const bool keepsSelection = shift || board_->selection().contains(s->id);
        board_->selectShape(s->id, keepsSelection);
        board_->bringToFront(s->id);
        startMove(point);
        return;
    }
    if (const Link *l = linkAt(point)) {
        board_->selectLink(l->id, shift);
        return;
    }
    if (shift) {
        drag_ = Drag::Rubber;
        return;
    }
    board_->clearSelection();
    drag_ = Drag::Pan;
    panStart_ = camera_;
}

bool Canvas::requestCodeAt(QPointF point)
{
    const Shape *shape = shapeAt(point);
    const std::optional<CodeReference> reference = shape ? shape->codeReferenceAt(point) : std::nullopt;
    if (!reference)
        return false;
    // The element stays selected: Back (Alt+←) comes back to it.
    board_->selectShape(shape->id);
    emit codeRequested(reference->name, reference->container, reference->filePath);
    return true;
}

void Canvas::startResize(Shape &shape)
{
    board_->measureShape(shape);
    dragBase_ = board_->snapshot();
    drag_ = Drag::Resize;
    dragShape_ = shape.id;
    resizeWidth_ = shape.w;
    resizeHeight_ = shape.h;
}

void Canvas::startMove(QPointF point)
{
    // Frames carry their content; the roots are the shapes the gesture
    // moves directly.
    const QSet<QString> carried = carriedBySelection();
    dragRoots_.clear();
    for (const QString &id : board_->selection()) {
        if (!carried.contains(id))
            dragRoots_.append(id);
    }
    QSet<QString> moving = board_->selection();
    moving.unite(carried);
    dragOffsets_.clear();
    for (const QString &id : moving) {
        if (const Shape *t = board_->shape(id))
            dragOffsets_.emplace_back(id, QPointF(point.x() - t->x, point.y() - t->y));
    }
    dragBase_ = board_->snapshot();
    drag_ = Drag::Move;
    update();
}

QSet<QString> Canvas::carriedBySelection() const
{
    QSet<QString> carried;
    for (const QString &id : board_->selection()) {
        const Shape *selected = board_->shape(id);
        if (!selected || !selected->movesChildren())
            continue;
        for (const Shape *child : board_->descendantShapes(selected))
            carried.insert(child->id);
    }
    return carried;
}

QSet<QString> Canvas::draggedIds() const
{
    QSet<QString> ids;
    for (const auto &[id, offset] : dragOffsets_)
        ids.insert(id);
    return ids;
}

void Canvas::moveDragged(QPointF point, bool snap)
{
    const qreal travel = std::hypot(point.x() - dragStart_.x(), point.y() - dragStart_.y());
    if (travel >= 3 / std::max(0.01, camera_.scale))
        moved_ = true;
    placeDragged(point);
    const Family *family = board_->family();
    guides_.clear();
    const bool aligns = family && family->alignmentGuides && snap;
    if (aligns)
        snapDragged();
    // With reparenting, the drag stays free (a child can be pulled out of its
    // frame); containment applies on release.
    const bool reparents = family && family->reparentOnDrop && moved_;
    if (reparents)
        dropTarget_ = dropTargetOfDragged();
    else
        containDragged();
    board_->touch();
}

void Canvas::placeDragged(QPointF point)
{
    for (const auto &[id, offset] : dragOffsets_) {
        if (Shape *s = board_->shape(id)) {
            s->x = point.x() - offset.x();
            s->y = point.y() - offset.y();
        }
    }
}

void Canvas::snapDragged()
{
    const QSet<QString> moving = draggedIds();
    const QRectF bounds = draggedBounds(*board_, dragOffsets_);
    const Snap result = snapAlignment(bounds, alignmentTargets(*board_, moving), 7 / std::max(0.01, camera_.scale));
    const bool snapped = result.dx != 0 || result.dy != 0;
    if (snapped) {
        for (const QString &id : moving) {
            if (Shape *s = board_->shape(id)) {
                s->x += result.dx;
                s->y += result.dy;
            }
        }
    }
    for (const auto &[vertical, values] : result.guides)
        guides_.push_back({vertical, values[0], values[1], values[2]});
}

QString Canvas::dropTargetOfDragged() const
{
    const QSet<QString> moving = draggedIds();
    for (const QString &id : dragRoots_) {
        const Shape *s = board_->shape(id);
        if (!s)
            continue;
        if (const auto decision = board_->dropDecision(*s, moving); decision && *decision)
            return (*decision)->id;
    }
    return {};
}

void Canvas::containDragged()
{
    for (const auto &[id, offset] : dragOffsets_) {
        if (Shape *s = board_->shape(id))
            board_->containShape(*s);
    }
}

void Canvas::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF screen = event->position();
    const QPointF p = world(screen);
    pointer_ = p;
    switch (drag_) {
    case Drag::None:
        updateHover(screen);
        return;
    case Drag::RightPending:
        if ((screen - dragStartScreen_).manhattanLength() < 4)
            return;
        // A right-drag pans, like the middle button.
        drag_ = Drag::Pan;
        setCursor(Qt::ClosedHandCursor);
        [[fallthrough]];
    case Drag::Pan:
        camera_.x = panStart_.x + (screen.x() - dragStartScreen_.x());
        camera_.y = panStart_.y + (screen.y() - dragStartScreen_.y());
        userCamera_ = true;
        syncEditor();
        update();
        return;
    case Drag::Move:
        moveDragged(p, !(event->modifiers() & Qt::AltModifier));
        return;
    case Drag::Resize:
        if (Shape *s = board_->shape(dragShape_)) {
            s->resizeTo(resizeWidth_ + p.x() - dragStart_.x(), resizeHeight_ + p.y() - dragStart_.y(), *board_);
            s->manualWidth = s->w;
            s->manualHeight = s->h;
            for (const auto &l : board_->links()) {
                if (l->from == s->id || l->to == s->id)
                    l->spline.reset();
            }
            board_->containShape(*s, true);
            board_->touch();
        }
        return;
    case Drag::Connect:
    case Drag::Rubber:
    case Drag::Trace:
        update();
        return;
    }
}

void Canvas::mouseReleaseEvent(QMouseEvent *event)
{
    const Drag drag = drag_;
    drag_ = Drag::None;
    const QPointF screen = event->position();
    const QPointF p = world(screen);
    guides_.clear();
    switch (drag) {
    case Drag::None:
    case Drag::Pan:
        break;
    case Drag::RightPending:
        showContextMenu(screen);
        break;
    case Drag::Rubber:
        board_->selectInRect(QRectF(dragStart_, p).normalized(), event->modifiers() & Qt::ShiftModifier);
        break;
    case Drag::Trace:
        finishTrace(p);
        break;
    case Drag::Connect:
        finishConnect(screen);
        break;
    case Drag::Move:
        finishMove();
        break;
    case Drag::Resize:
        board_->resolveLabels();
        board_->pushHistory(dragBase_);
        break;
    }
    dragBase_.clear();
    updateHover(screen);
    update();
}

void Canvas::finishTrace(QPointF point)
{
    if (!trace_)
        return;
    const qreal length = std::hypot(point.x() - dragStart_.x(), point.y() - dragStart_.y());
    if (length < 5 / std::max(0.01, camera_.scale))
        return;
    const qreal minX = std::min(dragStart_.x(), point.x());
    const qreal minY = std::min(dragStart_.y(), point.y());
    QJsonObject props = trace_->props;
    props.insert(QStringLiteral("x"), minX);
    props.insert(QStringLiteral("y"), minY);
    props.insert(QStringLiteral("points"), QJsonArray{QJsonArray{dragStart_.x() - minX, dragStart_.y() - minY},
                                                      QJsonArray{point.x() - minX, point.y() - minY}});
    if (Shape *created = board_->addShape(trace_->type, props)) {
        board_->selectShape(created->id);
        board_->containShape(*created);
        emit board_->elementsCreated({created->id}, {});
    }
    board_->pushHistory(dragBase_);
    trace_.reset();
}

void Canvas::finishMove()
{
    const Family *family = board_->family();
    const bool reparents = family && family->reparentOnDrop && moved_;
    if (reparents)
        reparentDragged();
    containDragged();
    dropTarget_.clear();
    board_->resolveLabels();
    board_->pushHistory(dragBase_);
}

void Canvas::reparentDragged()
{
    const QSet<QString> moving = draggedIds();
    for (const QString &id : dragRoots_) {
        Shape *s = board_->shape(id);
        if (!s)
            continue;
        const std::optional<Shape *> decision = board_->dropDecision(*s, moving);
        if (!decision)
            continue;
        const Shape *container = *decision;
        s->parentBoundary = container ? containerReference(*container) : QString();
        if (container)
            board_->containShape(*s, true);
    }
}

void Canvas::finishConnect(QPointF screen)
{
    const QPointF p = world(screen);
    const Shape *from = board_->shape(dragShape_);
    if (!from)
        return;
    const Shape *target = connectionShapeAt(p);
    QJsonObject portProps;
    if (!dragPort_.key.isEmpty())
        portProps.insert(QStringLiteral("fromPort"), dragPort_.key);
    if (target) {
        if (const std::optional<Port> port = target->connectionPortAt(*board_, p); port && !port->key.isEmpty())
            portProps.insert(QStringLiteral("toPort"), port->key);
        const Family *family = board_->family();
        if (family && !family->relations.isEmpty()) {
            chooseRelation(mapToGlobal(screen.toPoint()), from->id, target->id, portProps);
            return;
        }
        const QByteArray before = dragBase_;
        if (const Link *l = board_->connect(from->id, target->id, portProps)) {
            emit board_->elementsCreated({}, {l->id});
            board_->selectLink(l->id);
        }
        board_->resolveLabels();
        board_->pushHistory(before);
        return;
    }
    // Released in empty space: the next element, already linked.
    if (!shapeAt(p))
        chooseElement(mapToGlobal(screen.toPoint()), p, from->id);
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPointF p = world(event->position());
    if (Shape *s = shapeAt(p)) {
        board_->selectShape(s->id);
        s->onDoubleClick(*board_);
        return;
    }
    if (Link *l = linkAt(p)) {
        board_->selectLink(l->id);
        l->onDoubleClick(*board_);
        return;
    }
    chooseElement(mapToGlobal(event->position().toPoint()), p, {});
}

void Canvas::wheelEvent(QWheelEvent *event)
{
    // Ctrl+wheel or a plain wheel zoom; a touchpad's two-finger scroll pans.
    const QPoint pixels = event->pixelDelta();
    if (!pixels.isNull() && !(event->modifiers() & Qt::ControlModifier)) {
        camera_.x += pixels.x();
        camera_.y += pixels.y();
        userCamera_ = true;
        syncEditor();
        update();
        event->accept();
        return;
    }
    const qreal delta = event->angleDelta().y();
    if (delta)
        zoomAt(event->position(), std::exp(delta * 0.0015));
    event->accept();
}

void Canvas::leaveEvent(QEvent *)
{
    if (!hoverConnectionShape_.isEmpty()) {
        hoverConnectionShape_.clear();
        update();
    }
}

// ---------------------------------------------------------------------------
// Keyboard

bool Canvas::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride && isCanvasShortcut(static_cast<QKeyEvent *>(event))) {
        event->accept();
        return true;
    }
    if (event->type() == QEvent::KeyRelease && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Space &&
        !static_cast<QKeyEvent *>(event)->isAutoRepeat()) {
        spaceDown_ = false;
        unsetCursor();
    }
    return QWidget::event(event);
}

void Canvas::keyPressEvent(QKeyEvent *event)
{
    finishMotion();
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    const bool ctrl = modifiers & Qt::ControlModifier;
    if (ctrl && runControlShortcut(event->key(), modifiers))
        return;
    if (runEditingKey(*event, modifiers))
        return;
    QWidget::keyPressEvent(event);
}

bool Canvas::runControlShortcut(int key, Qt::KeyboardModifiers modifiers)
{
    switch (key) {
    case Qt::Key_Z:
        if (modifiers & Qt::ShiftModifier)
            board_->redo();
        else
            board_->undo();
        return true;
    case Qt::Key_Y:
        board_->redo();
        return true;
    case Qt::Key_C:
        copy();
        return true;
    case Qt::Key_X:
        cut();
        return true;
    case Qt::Key_V:
        paste();
        return true;
    case Qt::Key_A:
        board_->selectAll();
        return true;
    case Qt::Key_D:
        board_->duplicateSelection();
        return true;
    case Qt::Key_L:
        arrange(true);
        return true;
    case Qt::Key_0:
        resetZoom();
        return true;
    case Qt::Key_Equal:
    case Qt::Key_Plus:
        zoomBy(1.25);
        return true;
    case Qt::Key_Minus:
        zoomBy(1 / 1.25);
        return true;
    default:
        return false;
    }
}

// The keys that act with or without Ctrl, and F without it.
bool Canvas::runEditingKey(const QKeyEvent &event, Qt::KeyboardModifiers modifiers)
{
    switch (event.key()) {
    case Qt::Key_F:
        if (modifiers & Qt::ControlModifier)
            return false;
        fitView(true);
        return true;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        deleteSelection();
        return true;
    case Qt::Key_Escape:
        if (trace_) {
            trace_.reset();
            unsetCursor();
            return true;
        }
        board_->clearSelection();
        return true;
    case Qt::Key_F2:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        editSelection();
        return true;
    case Qt::Key_Space:
        if (!event.isAutoRepeat()) {
            spaceDown_ = true;
            setCursor(Qt::OpenHandCursor);
        }
        return true;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
        if (!board_->selection().isEmpty())
            nudgeSelection(arrowDelta(event.key(), (modifiers & Qt::ShiftModifier) ? 10 : 1));
        return true;
    default:
        return false;
    }
}

void Canvas::nudgeSelection(QPointF delta)
{
    board_->edit([&] {
        for (const QString &id : nudgedShapes()) {
            if (Shape *s = board_->shape(id)) {
                s->x += delta.x();
                s->y += delta.y();
                board_->containShape(*s);
            }
        }
    });
}

QSet<QString> Canvas::nudgedShapes() const
{
    QSet<QString> moving = board_->selection();
    for (const QString &id : board_->selection()) {
        const Shape *s = board_->shape(id);
        if (s && s->movesChildren()) {
            for (const Shape *child : board_->descendantShapes(s))
                moving.insert(child->id);
        }
    }
    return moving;
}

void Canvas::deleteSelection()
{
    if (!board_->allowDeletion()) {
        emit message(tr("The elements of this diagram cannot be deleted."));
        return;
    }
    board_->deleteSelection();
}

void Canvas::editSelection()
{
    if (Shape *s = board_->selectedShape())
        s->onDoubleClick(*board_);
    else if (Link *l = board_->selectedLink())
        l->onDoubleClick(*board_);
}

} // namespace diagram
