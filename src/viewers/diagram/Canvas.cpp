#include "Canvas.h"

#include "Board.h"
#include "Document.h"
#include "Family.h"
#include "Graphviz.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBlock>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace diagram {

namespace {

constexpr qreal geometryMs = 420;
constexpr qreal cameraMs = 380;
constexpr qreal appearMs = 260;
constexpr qreal ghostMs = 220;
const char *const selectionMime = "application/x-duet-diagram";

qreal clamp(qreal value, qreal low, qreal high)
{
    return std::max(low, std::min(value, high));
}

qreal easeOut(qreal t)
{
    t = clamp(t, 0, 1);
    return 1 - std::pow(1 - t, 3);
}

QRectF lerp(const QRectF &a, const QRectF &b, qreal t)
{
    return QRectF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t, a.width() + (b.width() - a.width()) * t,
                  a.height() + (b.height() - a.height()) * t);
}

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

// Keys that belong to the text being typed, not to the window (Ctrl+S
// still saves).
bool isTextEditingKey(const QKeyEvent *event)
{
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
    if (modifiers == Qt::NoModifier)
        return true;
    if (modifiers != Qt::ControlModifier)
        return false;
    switch (event->key()) {
    case Qt::Key_Z:
    case Qt::Key_Y:
    case Qt::Key_C:
    case Qt::Key_X:
    case Qt::Key_V:
    case Qt::Key_A:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return true;
    default:
        return false;
    }
}

QRectF shapeBox(const Shape &s)
{
    return s.rect();
}

// The smallest snapping of `moving` on the others' edges and centers.
struct Snap {
    qreal dx = 0, dy = 0;
    std::vector<std::pair<bool, std::array<qreal, 3>>> guides; // vertical, value, from, to
};

Snap snapAlignment(const QRectF &moving, const std::vector<QRectF> &targets, qreal threshold)
{
    Snap snap;
    const qreal movingX[] = {moving.left(), moving.center().x(), moving.right()};
    const qreal movingY[] = {moving.top(), moving.center().y(), moving.bottom()};
    std::optional<std::pair<qreal, std::pair<qreal, QRectF>>> vertical, horizontal; // delta, value, target
    for (const QRectF &target : targets) {
        const qreal targetX[] = {target.left(), target.center().x(), target.right()};
        const qreal targetY[] = {target.top(), target.center().y(), target.bottom()};
        for (qreal source : movingX) {
            for (qreal value : targetX) {
                const qreal delta = value - source;
                if (std::abs(delta) <= threshold && (!vertical || std::abs(delta) < std::abs(vertical->first)))
                    vertical = {delta, {value, target}};
            }
        }
        for (qreal source : movingY) {
            for (qreal value : targetY) {
                const qreal delta = value - source;
                if (std::abs(delta) <= threshold && (!horizontal || std::abs(delta) < std::abs(horizontal->first)))
                    horizontal = {delta, {value, target}};
            }
        }
    }
    snap.dx = vertical ? vertical->first : 0;
    snap.dy = horizontal ? horizontal->first : 0;
    const QRectF shifted = moving.translated(snap.dx, snap.dy);
    if (vertical) {
        const QRectF &t = vertical->second.second;
        snap.guides.push_back(
            {true, {vertical->second.first, std::min(shifted.top(), t.top()), std::max(shifted.bottom(), t.bottom())}});
    }
    if (horizontal) {
        const QRectF &t = horizontal->second.second;
        snap.guides.push_back(
            {false,
             {horizontal->second.first, std::min(shifted.left(), t.left()), std::max(shifted.right(), t.right())}});
    }
    return snap;
}

void drawArrowPreview(QPainter &painter, QPointF a, QPointF b, const Style &style, const QList<qreal> &dash = {},
                      bool head = true)
{
    const qreal angle = std::atan2(b.y() - a.y(), b.x() - a.x());
    const qreal inset = head ? arrowBaseInset(style.link.arrow) : 0;
    strokeLine(painter, a, QPointF(b.x() - inset * std::cos(angle), b.y() - inset * std::sin(angle)), style.link.color,
               style.link.width, dash);
    if (head)
        fillArrowHead(painter, b, angle, style.link.arrow, style.link.color);
}

} // namespace

Canvas::Canvas(Board *board, QWidget *parent)
    : QWidget(parent)
    , board_(board)
    , editor_(new QPlainTextEdit(this))
    , animationTimer_(new QTimer(this))
    , routeTimer_(new QTimer(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(120, 120);

    editor_->hide();
    editor_->setFrameShape(QFrame::NoFrame);
    editor_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    editor_->document()->setDocumentMargin(0);
    editor_->installEventFilter(this);
    connect(editor_, &QPlainTextEdit::textChanged, this, [this] {
        if (!editSpec_.set)
            return;
        editSpec_.set(editor_->toPlainText());
        if (Shape *s = board_->shape(editShape_)) {
            // A box growing while typed stays anchored where it was: its links
            // do not jump at every character.
            const QPointF origin(s->x, s->y), center(s->cx(), s->cy());
            board_->measureShape(*s);
            if (s->editResizeAnchorTopLeft()) {
                s->x = origin.x();
                s->y = origin.y();
            } else {
                s->x = center.x() - s->w / 2;
                s->y = center.y() - s->h / 2;
            }
            keepVisible(*s);
        }
        syncEditor();
        board_->touch();
    });

    animationTimer_->setInterval(16);
    connect(animationTimer_, &QTimer::timeout, this, &Canvas::stepAnimation);
    clock_.start();
    routeTimer_->setSingleShot(true);
    routeTimer_->setInterval(0);
    connect(routeTimer_, &QTimer::timeout, this, [this] {
        if (drag_ == Drag::None && graphviz::routeLinks(*board_))
            update();
    });

    connect(board_, &Board::changed, this, qOverload<>(&QWidget::update));
    connect(board_, &Board::selectionChanged, this, qOverload<>(&QWidget::update));
    connect(board_, &Board::geometrySettled, this, [this] {
        const Family *family = board_->family();
        if (family && family->routeLinksOnMove && !arranging_)
            routeLinksSoon();
    });
    connect(board_, &Board::elementsCreated, this, [this](const QStringList &shapes, const QStringList &) {
        for (const QString &id : shapes)
            appearing_.insert(id, {qreal(clock_.elapsed()), appearMs});
        startAnimation();
    });
    connect(board_, &Board::removing, this,
            [this](const QList<const Shape *> &shapes, const QList<const Link *> &links) {
                if (!isVisible() || board_->shapes().size() >= 200)
                    return;
                const qreal now = clock_.elapsed();
                for (const Shape *s : shapes) {
                    if (std::shared_ptr<Shape> copy = board_->instantiate(s->type, s->toJson()))
                        ghosts_.push_back({copy, {}, {}, now});
                }
                for (const Link *l : links) {
                    if (const std::optional<Route> r = l->route(*board_))
                        ghosts_.push_back({nullptr, l->routePolyline(*r), board_->style().link.color, now});
                }
                startAnimation();
            });
    board_->setTextEditor(
        [this](Shape *shape, Link *link, const TextEditSpec &spec) { beginTextEdit(shape, link, spec); });
}

Canvas::~Canvas()
{
    board_->setTextEditor({});
}

void Canvas::setGridVisible(bool visible)
{
    gridVisible_ = visible;
    update();
}

QPointF Canvas::viewCenter() const
{
    return world(QPointF(rect().center()));
}

bool Canvas::isEditingText() const
{
    return editor_->isVisible();
}

// ---------------------------------------------------------------------------
// View

void Canvas::fitView(bool animate)
{
    if (board_->isEmpty() || width() < 2 || height() < 2)
        return;
    board_->measureAll();
    const QRectF box = board_->contentBounds();
    if (!box.isValid() && box.isNull())
        return;
    const Style &style = board_->style();
    const qreal shortSide = std::min(width(), height());
    const qreal inset = clamp(clamp(shortSide * 0.04, 16, 48), 0, shortSide / 4);
    const qreal scale = std::min({(width() - inset * 2) / std::max<qreal>(1, box.width()),
                                  (height() - inset * 2) / std::max<qreal>(1, box.height()),
                                  std::min(style.maxScale, style.fitMaxScale)});
    Camera target;
    target.scale = scale;
    target.x = (width() - (box.left() + box.right()) * scale) / 2;
    target.y = (height() - (box.top() + box.bottom()) * scale) / 2;
    userCamera_ = false;
    if (animate) {
        animateCamera(target);
    } else {
        camera_ = target;
        emit zoomChanged(camera_.scale);
        syncEditor();
        update();
    }
}

void Canvas::zoomBy(qreal factor)
{
    zoomAt(QPointF(rect().center()), factor);
}

void Canvas::resetZoom()
{
    zoomAt(QPointF(rect().center()), 1 / camera_.scale);
}

void Canvas::zoomAt(QPointF screen, qreal factor)
{
    cameraAnimating_ = false;
    const QPointF w = world(screen);
    const Style &style = board_->style();
    const qreal scale = clamp(camera_.scale * factor, std::min(camera_.scale, style.minScale), style.maxScale);
    camera_.x = screen.x() - w.x() * scale;
    camera_.y = screen.y() - w.y() * scale;
    camera_.scale = scale;
    userCamera_ = true;
    emit zoomChanged(scale);
    syncEditor();
    update();
}

void Canvas::animateCamera(const Camera &to)
{
    cameraFrom_ = camera_;
    cameraTo_ = to;
    cameraStart_ = clock_.elapsed();
    cameraAnimating_ = true;
    startAnimation();
}

void Canvas::startAnimation()
{
    if (!animationTimer_->isActive())
        animationTimer_->start();
}

void Canvas::stepAnimation()
{
    const qreal now = clock_.elapsed();
    bool running = false;
    if (cameraAnimating_) {
        const qreal t = easeOut((now - cameraStart_) / cameraMs);
        camera_.x = cameraFrom_.x + (cameraTo_.x - cameraFrom_.x) * t;
        camera_.y = cameraFrom_.y + (cameraTo_.y - cameraFrom_.y) * t;
        camera_.scale = cameraFrom_.scale + (cameraTo_.scale - cameraFrom_.scale) * t;
        emit zoomChanged(camera_.scale);
        syncEditor();
        if (t >= 1)
            cameraAnimating_ = false;
        else
            running = true;
    }
    if (geometryStart_ >= 0) {
        if (now - geometryStart_ >= geometryMs) {
            geometryStart_ = -1;
            geometryFrom_.clear();
            geometryTo_.clear();
            splinesFrom_.clear();
        } else {
            running = true;
        }
    }
    for (auto it = appearing_.begin(); it != appearing_.end();) {
        if (now - it->start >= it->duration)
            it = appearing_.erase(it);
        else {
            running = true;
            ++it;
        }
    }
    ghosts_.erase(
        std::remove_if(ghosts_.begin(), ghosts_.end(), [now](const Ghost &g) { return now - g.start >= ghostMs; }),
        ghosts_.end());
    running = running || !ghosts_.empty();
    if (!running)
        animationTimer_->stop();
    update();
}

void Canvas::finishMotion()
{
    if (cameraAnimating_) {
        camera_ = cameraTo_;
        cameraAnimating_ = false;
        emit zoomChanged(camera_.scale);
    }
    geometryStart_ = -1;
    geometryFrom_.clear();
    geometryTo_.clear();
    splinesFrom_.clear();
    update();
}

QHash<QString, QRectF> Canvas::captureGeometry() const
{
    QHash<QString, QRectF> result;
    for (const auto &s : board_->shapes())
        result.insert(s->id, shapeBox(*s));
    return result;
}

void Canvas::animateGeometry(const QHash<QString, QRectF> &before, const Camera *cameraTo)
{
    // The model is already at its destination: only the drawing moves, so
    // saving, history and exports see the finished operation.
    geometryFrom_ = before;
    geometryTo_ = captureGeometry();
    geometryStart_ = clock_.elapsed();
    if (cameraTo)
        animateCamera(*cameraTo);
    startAnimation();
}

template <typename Fn>
void Canvas::withVisualGeometry(Fn draw)
{
    if (geometryStart_ < 0 || geometryFrom_.isEmpty()) {
        draw();
        return;
    }
    const qreal t = easeOut((clock_.elapsed() - geometryStart_) / geometryMs);
    struct Saved {
        Shape *shape;
        QRectF box;
    };
    std::vector<Saved> shapes;
    for (const auto &s : board_->shapes()) {
        const auto from = geometryFrom_.constFind(s->id);
        const auto to = geometryTo_.constFind(s->id);
        if (from == geometryFrom_.cend() || to == geometryTo_.cend())
            continue;
        shapes.push_back({s.get(), shapeBox(*s)});
        const QRectF view = lerp(*from, *to, t);
        s->x = view.x();
        s->y = view.y();
        s->w = view.width();
        s->h = view.height();
    }
    // Each final curve is warped with its two ends, so that no arrow ever
    // detaches from its node.
    struct SavedLink {
        Link *link;
        std::optional<Spline> spline;
        std::optional<Link::ReservedLabel> label;
    };
    std::vector<SavedLink> links;
    for (const auto &l : board_->links()) {
        links.push_back({l.get(), l->spline, l->layoutLabelPoint});
        if (!l->spline)
            continue;
        const Shape *a = board_->shape(l->from);
        const Shape *b = board_->shape(l->to);
        if (!a || !b)
            continue;
        const auto endA = std::find_if(shapes.begin(), shapes.end(), [a](const Saved &s) { return s.shape == a; });
        const auto endB = std::find_if(shapes.begin(), shapes.end(), [b](const Saved &s) { return s.shape == b; });
        const QRectF finalA = endA != shapes.end() ? endA->box : a->rect();
        const QRectF finalB = endB != shapes.end() ? endB->box : b->rect();
        Spline spline = *l->spline;
        const QPointF anchorA = spline.start.value_or(spline.points.front());
        const QPointF anchorB = spline.end.value_or(spline.points.back());
        const qreal dxA =
            a->x - finalA.x() + (anchorA.x() - finalA.x()) * (a->w / (finalA.width() ? finalA.width() : 1) - 1);
        const qreal dyA =
            a->y - finalA.y() + (anchorA.y() - finalA.y()) * (a->h / (finalA.height() ? finalA.height() : 1) - 1);
        const qreal dxB =
            b->x - finalB.x() + (anchorB.x() - finalB.x()) * (b->w / (finalB.width() ? finalB.width() : 1) - 1);
        const qreal dyB =
            b->y - finalB.y() + (anchorB.y() - finalB.y()) * (b->h / (finalB.height() ? finalB.height() : 1) - 1);
        const auto shift = [&](QPointF p, qreal u) {
            return QPointF(p.x() + dxA * (1 - u) + dxB * u, p.y() + dyA * (1 - u) + dyB * u);
        };
        const size_t n = spline.points.size();
        for (size_t i = 0; i < n; ++i)
            spline.points[i] = shift(spline.points[i], qreal(i) / std::max<size_t>(1, n - 1));
        // Morph from the previous curve when it had the same shape.
        const auto old = splinesFrom_.constFind(l->id);
        if (old != splinesFrom_.cend() && old->points.size() == n) {
            for (size_t i = 1; i + 1 < n; ++i)
                spline.points[i] = old->points[i] * (1 - t) + spline.points[i] * t;
        }
        if (spline.start)
            spline.start = shift(*spline.start, 0);
        if (spline.end)
            spline.end = shift(*spline.end, 1);
        if (spline.labelPoint)
            spline.labelPoint = shift(*spline.labelPoint, 0.5);
        spline.fromAnchor = QPointF(a->x, a->y);
        spline.toAnchor = QPointF(b->x, b->y);
        l->spline = spline;
        if (l->layoutLabelPoint)
            l->layoutLabelPoint =
                Link::ReservedLabel{shift(l->layoutLabelPoint->point, 0.5), QPointF(a->x, a->y), QPointF(b->x, b->y)};
    }
    draw();
    for (const Saved &saved : shapes) {
        saved.shape->x = saved.box.x();
        saved.shape->y = saved.box.y();
        saved.shape->w = saved.box.width();
        saved.shape->h = saved.box.height();
    }
    for (const SavedLink &saved : links) {
        saved.link->spline = saved.spline;
        saved.link->layoutLabelPoint = saved.label;
    }
}

bool Canvas::arrange(bool animate)
{
    const Family *family = board_->family();
    if (!family || board_->isEmpty())
        return true;
    endTextEdit(true);
    finishMotion();
    const QHash<QString, QRectF> before = captureGeometry();
    const Camera cameraBefore = camera_;
    bool ok = true;
    arranging_ = true;
    if (family->autoArrange) {
        // A family with a layout of its own (sequence lanes): Graphviz would
        // break its geometry.
        board_->edit([&] {
            board_->measureAll();
            family->autoArrange(*board_);
        });
    } else {
        QString error;
        ok = graphviz::autoArrange(*board_, arrangeOptions(), &error);
        if (!ok)
            emit message(tr("The layout failed: %1").arg(error));
    }
    arranging_ = false;
    fitView(false);
    const Camera target = camera_;
    if (animate && isVisible()) {
        camera_ = cameraBefore;
        animateGeometry(before, &target);
    }
    update();
    return ok;
}

graphviz::ArrangeOptions Canvas::arrangeOptions() const
{
    graphviz::ArrangeOptions options;
    const Family *family = board_->family();
    options.rankdir = board_->meta.value(QStringLiteral("rankdir")).toString(QStringLiteral("TB"));
    options.engine = board_->meta.value(QStringLiteral("engine")).toString(QStringLiteral("dot"));
    options.root = board_->meta.value(QStringLiteral("root")).toString();
    if (family) {
        options.clusters = family->graphvizClusters;
        options.inferClusterMembership = family->inferClusterMembership;
        options.stackDisconnected = family->graphvizStackDisconnected;
        options.adaptiveDirection = family->graphvizAdaptiveDirection;
        options.splines = family->graphvizSplines;
    }
    options.viewSize = size();
    return options;
}

void Canvas::routeLinksSoon()
{
    routeTimer_->start();
}

void Canvas::settle(const document::Loaded &loaded, bool animate)
{
    const Family *family = board_->family();
    if (!family)
        return;
    if (loaded.needsArrange) {
        arrange(animate);
    } else if (!loaded.unplaced.isEmpty()) {
        if (family->autoArrange) {
            // Its own layout follows the source's order: it places everything.
            arrange(animate);
        } else {
            const QHash<QString, QRectF> before = captureGeometry();
            graphviz::placeNewcomers(*board_, loaded.unplaced, arrangeOptions());
            if (family->routeLinksOnMove)
                graphviz::routeLinks(*board_);
            if (animate && isVisible())
                animateGeometry(before);
        }
    } else if (loaded.needsRouting) {
        graphviz::routeLinks(*board_);
    }
    update();
}

// ---------------------------------------------------------------------------
// Painting

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
    const qreal ox = std::fmod(camera_.x, step), oy = std::fmod(camera_.y, step);
    const qreal r = clamp(step * 0.045, 0.8, 1.6);
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(board_->style().gridColor);
    for (qreal x = ox; x < area.right(); x += step) {
        for (qreal y = oy; y < area.bottom(); y += step)
            painter.drawEllipse(QPointF(x, y), r, r);
    }
    painter.restore();
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
    withVisualGeometry([&] {
        const qreal now = clock_.elapsed();
        const auto drawShape = [&](const Shape &s, bool decorate) {
            const bool selected = board_->selection().contains(s.id);
            const auto appear = appearing_.constFind(s.id);
            if (appear != appearing_.cend()) {
                const qreal t = easeOut((now - appear->start) / appear->duration);
                painter.save();
                painter.setOpacity(t);
                const qreal scale = 0.96 + 0.04 * t;
                painter.translate(s.cx(), s.cy());
                painter.scale(scale, scale);
                painter.translate(-s.cx(), -s.cy());
            }
            ShapeState state;
            state.selected = selected;
            state.editing = s.id == editShape_ && isEditingText();
            state.dropTarget = s.id == dropTarget_;
            s.draw(painter, *board_, state);
            if (appear != appearing_.cend())
                painter.restore();
            if (state.dropTarget) {
                QPainterPath halo = roundRect(s.x - 3, s.y - 3, s.w + 6, s.h + 6, style.shape.radius + 3);
                stroke(painter, halo, style.shape.selectedStroke, 2 / camera_.scale,
                       {6 / camera_.scale, 4 / camera_.scale});
            }
            if (decorate && selected)
                drawSelectionHalo(painter, s);
            if (decorate && portsVisible(s))
                drawPorts(painter, s);
            if (decorate && selected && s.resizable())
                drawResizeHandle(painter, s);
        };
        if (const Family *family = board_->family(); family && family->drawUnderlay)
            family->drawUnderlay(painter, *board_);
        // Containers first, ordered parents before children and larger
        // before smaller: they never hide a relation.
        std::vector<const Shape *> background, foreground;
        for (const auto &s : board_->shapes())
            (s->drawBehindLinks() ? background : foreground).push_back(s.get());
        const auto depth = [this](const Shape *s) {
            int value = 0;
            const Shape *current = s;
            QSet<const Shape *> seen{s};
            while (current && !current->parentBoundary.isEmpty()) {
                current = board_->shapeByReference(current->parentBoundary);
                if (!current || seen.contains(current))
                    break;
                seen.insert(current);
                ++value;
            }
            return value;
        };
        std::stable_sort(background.begin(), background.end(), [&depth](const Shape *a, const Shape *b) {
            const int da = depth(a), db = depth(b);
            return da != db ? da < db : a->w * a->h > b->w * b->h;
        });
        for (const Shape *s : background)
            drawShape(*s, false);
        for (const auto &l : board_->links())
            l->draw(painter, *board_, board_->linkSelection().contains(l->id));
        if (drag_ == Drag::Connect) {
            const QPointF start(dragPort_.x, dragPort_.y);
            QPointF end = pointer_;
            const Shape *from = board_->shape(dragShape_);
            const Shape *target = connectionShapeAt(pointer_);
            if (target && target != from)
                end = target->connectionPoint(*board_, start, pointer_);
            drawArrowPreview(painter, start, end, style);
        }
        if (drag_ == Drag::Trace && trace_) {
            const QString arrow = trace_->props.value(QStringLiteral("arrow")).toString(QStringLiteral("none"));
            const QString line = trace_->props.value(QStringLiteral("line")).toString(QStringLiteral("solid"));
            const QList<qreal> dash = line == QLatin1String("dashed")   ? QList<qreal>{8, 5}
                                      : line == QLatin1String("dotted") ? QList<qreal>{2, 5}
                                                                        : QList<qreal>{};
            if (arrow == QLatin1String("start"))
                drawArrowPreview(painter, pointer_, dragStart_, style, dash);
            else
                drawArrowPreview(painter, dragStart_, pointer_, style, dash,
                                 arrow == QLatin1String("end") || arrow == QLatin1String("both"));
        }
        for (const Shape *s : foreground)
            drawShape(*s, true);
        for (const Ghost &ghost : ghosts_) {
            painter.save();
            painter.setOpacity(1 - clamp((now - ghost.start) / ghostMs, 0, 1));
            if (ghost.shape)
                ghost.shape->draw(painter, *board_, ShapeState{});
            else if (ghost.line.size() > 1) {
                QPainterPath path;
                path.moveTo(ghost.line.front());
                for (size_t i = 1; i < ghost.line.size(); ++i)
                    path.lineTo(ghost.line[i]);
                stroke(painter, path, ghost.color, style.link.width);
            }
            painter.restore();
        }
        if (!guides_.empty()) {
            // A guide points at an alignment, it does not designate anything:
            // in the guide color rather than the selection's.
            const qreal pad = 16 / camera_.scale;
            for (const Guide &g : guides_) {
                const QPointF a = g.vertical ? QPointF(g.value, g.from - pad) : QPointF(g.from - pad, g.value);
                const QPointF b = g.vertical ? QPointF(g.value, g.to + pad) : QPointF(g.to + pad, g.value);
                strokeLine(painter, a, b, style.shape.guide, 1 / camera_.scale, {5 / camera_.scale, 4 / camera_.scale});
            }
        }
        // A selected container's handles stay above its relations.
        for (const Shape *s : background) {
            const bool selected = board_->selection().contains(s->id);
            if (selected)
                drawSelectionHalo(painter, *s);
            if (portsVisible(*s))
                drawPorts(painter, *s);
            if (selected && s->resizable())
                drawResizeHandle(painter, *s);
        }
        // Labels and selected endpoints last, above the shapes they cross.
        for (const auto &l : board_->links()) {
            l->drawLabel(painter, *board_, l->id == editLink_ && isEditingText());
            if (board_->linkSelection().contains(l->id))
                l->drawEndpoints(painter, *board_);
        }
    });
    painter.restore();
    if (drag_ == Drag::Rubber) {
        const QRectF band = QRectF(dragStartScreen_, camera_.toScreen(pointer_)).normalized();
        painter.save();
        painter.setPen(QPen(style.shape.selectedStroke, 1));
        painter.setBrush(withAlpha(style.shape.selectedStroke, 0.08));
        painter.drawRect(band);
        painter.restore();
    }
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
    std::vector<const Shape *> background, foreground;
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
    for (const auto &l : board.links())
        l->drawLabel(painter, board, false);
}

QIcon Canvas::presetIcon(const Family &family, const ElementPreset &preset, const Style &style)
{
    Board board;
    board.setFamily(&family);
    board.setStyle(style);
    Shape *s = board.addShape(preset.shapeType, preset.props);
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
    return QIcon(pixmap);
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
    return QIcon(pixmap);
}

// ---------------------------------------------------------------------------
// Hit tests

Shape *Canvas::shapeAt(QPointF point) const
{
    const auto &shapes = board_->shapes();
    for (auto it = shapes.rbegin(); it != shapes.rend(); ++it) {
        if ((*it)->hitTest(point))
            return it->get();
    }
    return nullptr;
}

Shape *Canvas::connectionShapeAt(QPointF point) const
{
    const auto &shapes = board_->shapes();
    for (auto it = shapes.rbegin(); it != shapes.rend(); ++it) {
        if ((*it)->connectionHitTest(*board_, point))
            return it->get();
    }
    return nullptr;
}

Link *Canvas::linkAt(QPointF point) const
{
    const qreal tolerance = 6 / camera_.scale;
    const auto &links = board_->links();
    for (auto it = links.rbegin(); it != links.rend(); ++it) {
        const qreal width = (*it)->customWidth.value_or(board_->style().link.width);
        if ((*it)->hitTest(*board_, point, std::max(tolerance, width / 2)))
            return it->get();
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
    Shape *s = port ? port->first : shapeAt(p);
    if (!s)
        s = connectionShapeAt(p);
    const QString hover = s && s->showConnectionPortsOnHover() ? s->id : QString();
    if (hover != hoverConnectionShape_) {
        hoverConnectionShape_ = hover;
        update();
    }
    if (portAt(p))
        setCursor(Qt::CrossCursor);
    else if (s)
        setCursor(Qt::SizeAllCursor);
    else if (linkAt(p))
        setCursor(Qt::PointingHandCursor);
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
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && spaceDown_)) {
        drag_ = Drag::Pan;
        panStart_ = camera_;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() == Qt::RightButton) {
        drag_ = Drag::RightPending;
        panStart_ = camera_;
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    if (trace_) {
        drag_ = Drag::Trace;
        dragBase_ = board_->snapshot();
        return;
    }
    if (Shape *s = resizeHandleAt(p)) {
        board_->measureShape(*s);
        dragBase_ = board_->snapshot();
        drag_ = Drag::Resize;
        dragShape_ = s->id;
        resizeWidth_ = s->w;
        resizeHeight_ = s->h;
        return;
    }
    if (const auto port = portAt(p)) {
        dragBase_ = board_->snapshot();
        drag_ = Drag::Connect;
        dragShape_ = port->first->id;
        dragPort_ = port->second;
        return;
    }
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    if (Shape *s = shapeAt(p)) {
        if (!shift && !board_->selection().contains(s->id))
            board_->selectShape(s->id);
        else
            board_->selectShape(s->id, true);
        board_->bringToFront(s->id);
        // Frames carry their content; the roots are the shapes the gesture
        // moves directly.
        QSet<QString> carried;
        for (const QString &id : board_->selection()) {
            const Shape *selected = board_->shape(id);
            if (!selected || !selected->movesChildren())
                continue;
            for (const Shape *child : board_->descendantShapes(selected))
                carried.insert(child->id);
        }
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
                dragOffsets_.push_back({id, QPointF(p.x() - t->x, p.y() - t->y)});
        }
        dragBase_ = board_->snapshot();
        drag_ = Drag::Move;
        update();
        return;
    }
    if (Link *l = linkAt(p)) {
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

void Canvas::moveDragged(QPointF p, bool snap)
{
    if (std::hypot(p.x() - dragStart_.x(), p.y() - dragStart_.y()) >= 3 / std::max(0.01, camera_.scale))
        moved_ = true;
    for (const auto &[id, offset] : dragOffsets_) {
        if (Shape *s = board_->shape(id)) {
            s->x = p.x() - offset.x();
            s->y = p.y() - offset.y();
        }
    }
    const Family *family = board_->family();
    guides_.clear();
    if (family && family->alignmentGuides && snap) {
        QSet<QString> moving;
        QRectF movingBounds;
        for (const auto &[id, offset] : dragOffsets_) {
            moving.insert(id);
            if (const Shape *s = board_->shape(id)) {
                const QRectF box = s->bounds(*board_).value_or(s->rect());
                movingBounds = movingBounds.isNull() ? box : movingBounds.united(box);
            }
        }
        // Each shape's full extent (a stroke and its label); the hidden ends
        // of free arrows align nothing.
        std::vector<QRectF> targets;
        for (const auto &s : board_->shapes()) {
            if (!moving.contains(s->id) && s->type != QLatin1String("freeAnchor"))
                targets.push_back(s->bounds(*board_).value_or(s->rect()));
        }
        const Snap result = snapAlignment(movingBounds, targets, 7 / std::max(0.01, camera_.scale));
        if (result.dx || result.dy) {
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
    // With reparenting, the drag stays free (a child can be pulled out of its
    // frame); containment applies on release.
    if (family && family->reparentOnDrop && moved_) {
        QSet<QString> moving;
        for (const auto &[id, offset] : dragOffsets_)
            moving.insert(id);
        QString target;
        for (const QString &id : dragRoots_) {
            const Shape *s = board_->shape(id);
            if (!s)
                continue;
            if (const auto decision = board_->dropDecision(*s, moving); decision && *decision) {
                target = (*decision)->id;
                break;
            }
        }
        dropTarget_ = target;
    } else {
        for (const auto &[id, offset] : dragOffsets_) {
            if (Shape *s = board_->shape(id))
                board_->containShape(*s);
        }
    }
    board_->touch();
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
        break;
    case Drag::RightPending:
        showContextMenu(screen);
        break;
    case Drag::Pan:
        break;
    case Drag::Rubber:
        board_->selectInRect(QRectF(dragStart_, p).normalized(), event->modifiers() & Qt::ShiftModifier);
        break;
    case Drag::Trace:
        if (trace_ && std::hypot(p.x() - dragStart_.x(), p.y() - dragStart_.y()) >= 5 / std::max(0.01, camera_.scale)) {
            const qreal minX = std::min(dragStart_.x(), p.x()), minY = std::min(dragStart_.y(), p.y());
            QJsonObject props = trace_->props;
            props.insert(QStringLiteral("x"), minX);
            props.insert(QStringLiteral("y"), minY);
            props.insert(QStringLiteral("points"), QJsonArray{QJsonArray{dragStart_.x() - minX, dragStart_.y() - minY},
                                                              QJsonArray{p.x() - minX, p.y() - minY}});
            if (Shape *created = board_->addShape(trace_->type, props)) {
                board_->selectShape(created->id);
                board_->containShape(*created);
                emit board_->elementsCreated({created->id}, {});
            }
            board_->pushHistory(dragBase_);
            trace_.reset();
        }
        break;
    case Drag::Connect:
        finishConnect(screen);
        break;
    case Drag::Move: {
        const Family *family = board_->family();
        if (family && family->reparentOnDrop && moved_) {
            QSet<QString> moving;
            for (const auto &[id, offset] : dragOffsets_)
                moving.insert(id);
            for (const QString &id : dragRoots_) {
                Shape *s = board_->shape(id);
                if (!s)
                    continue;
                const auto decision = board_->dropDecision(*s, moving);
                if (!decision)
                    continue;
                s->parentBoundary =
                    *decision ? ((*decision)->key.isEmpty() ? (*decision)->id : (*decision)->key) : QString();
                if (*decision)
                    board_->containShape(*s, true);
            }
        }
        for (const auto &[id, offset] : dragOffsets_) {
            if (Shape *s = board_->shape(id))
                board_->containShape(*s);
        }
        dropTarget_.clear();
        board_->resolveLabels();
        board_->pushHistory(dragBase_);
        break;
    }
    case Drag::Resize:
        board_->resolveLabels();
        board_->pushHistory(dragBase_);
        break;
    }
    dragBase_.clear();
    updateHover(screen);
    update();
}

void Canvas::finishConnect(QPointF screen)
{
    const QPointF p = world(screen);
    Shape *from = board_->shape(dragShape_);
    if (!from)
        return;
    Shape *target = connectionShapeAt(p);
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
        if (Link *l = board_->connect(from->id, target->id, portProps)) {
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
    const bool shift = modifiers & Qt::ShiftModifier;
    switch (event->key()) {
    case Qt::Key_Z:
        if (ctrl) {
            shift ? board_->redo() : board_->undo();
            return;
        }
        break;
    case Qt::Key_Y:
        if (ctrl) {
            board_->redo();
            return;
        }
        break;
    case Qt::Key_C:
        if (ctrl) {
            copy();
            return;
        }
        break;
    case Qt::Key_X:
        if (ctrl) {
            cut();
            return;
        }
        break;
    case Qt::Key_V:
        if (ctrl) {
            paste();
            return;
        }
        break;
    case Qt::Key_A:
        if (ctrl) {
            board_->selectAll();
            return;
        }
        break;
    case Qt::Key_D:
        if (ctrl) {
            board_->duplicateSelection();
            return;
        }
        break;
    case Qt::Key_L:
        if (ctrl) {
            arrange(true);
            return;
        }
        break;
    case Qt::Key_0:
        if (ctrl) {
            resetZoom();
            return;
        }
        break;
    case Qt::Key_Equal:
    case Qt::Key_Plus:
        if (ctrl) {
            zoomBy(1.25);
            return;
        }
        break;
    case Qt::Key_Minus:
        if (ctrl) {
            zoomBy(1 / 1.25);
            return;
        }
        break;
    case Qt::Key_F:
        if (!ctrl) {
            fitView(true);
            return;
        }
        break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        deleteSelection();
        return;
    case Qt::Key_Escape:
        if (trace_) {
            trace_.reset();
            unsetCursor();
            return;
        }
        board_->clearSelection();
        return;
    case Qt::Key_F2:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        editSelection();
        return;
    case Qt::Key_Space:
        if (!event->isAutoRepeat()) {
            spaceDown_ = true;
            setCursor(Qt::OpenHandCursor);
        }
        return;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        if (board_->selection().isEmpty())
            return;
        const qreal step = shift ? 10 : 1;
        const QPointF delta(event->key() == Qt::Key_Left    ? -step
                            : event->key() == Qt::Key_Right ? step
                                                            : 0,
                            event->key() == Qt::Key_Up     ? -step
                            : event->key() == Qt::Key_Down ? step
                                                           : 0);
        board_->edit([&] {
            QSet<QString> moving = board_->selection();
            for (const QString &id : board_->selection()) {
                const Shape *s = board_->shape(id);
                if (s && s->movesChildren()) {
                    for (const Shape *child : board_->descendantShapes(s))
                        moving.insert(child->id);
                }
            }
            for (const QString &id : moving) {
                if (Shape *s = board_->shape(id)) {
                    s->x += delta.x();
                    s->y += delta.y();
                    board_->containShape(*s);
                }
            }
        });
        return;
    }
    default:
        break;
    }
    QWidget::keyPressEvent(event);
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

// ---------------------------------------------------------------------------
// Clipboard

void Canvas::copy()
{
    if (!board_->copySelection())
        return;
    const QJsonObject json = board_->selectionJson();
    auto *data = new QMimeData;
    data->setData(QString::fromLatin1(selectionMime), QJsonDocument(json).toJson(QJsonDocument::Compact));
    // The selection in the family's notation too, for a text editor or
    // Claude.
    if (const Codec *codec = board_->family() ? defaultCodec(*board_->family()) : nullptr) {
        Board selection;
        selection.setFamily(board_->family());
        selection.fromJson(json);
        selection.meta = board_->meta;
        data->setText(codec->serialize(selection, board_->meta));
    }
    QApplication::clipboard()->setMimeData(data);
    emit message(tr("Copied"));
}

void Canvas::cut()
{
    copy();
    deleteSelection();
}

void Canvas::paste()
{
    const QMimeData *data = QApplication::clipboard()->mimeData();
    if (data && data->hasFormat(QString::fromLatin1(selectionMime))) {
        const QJsonObject json = QJsonDocument::fromJson(data->data(QString::fromLatin1(selectionMime))).object();
        if (board_->family() && json.value(QStringLiteral("family")).toString() == board_->family()->id) {
            const QPointF center =
                board_->hasClipboard() && board_->selection().isEmpty() ? viewCenter() : viewCenter() + QPointF(24, 24);
            board_->pasteJson(json, center);
            return;
        }
    }
    if (data && data->hasText()) {
        insertSource(data->text());
        return;
    }
    board_->pasteClipboard();
}

void Canvas::insertSource(const QString &text)
{
    // A source pasted (Mermaid written elsewhere): its elements, laid out,
    // join the diagram near the center of the view.
    const Family *family = board_->family();
    if (!family)
        return;
    for (const Codec *codec : allCodecs()) {
        if (&codec->family() != family || !codec->matches(text))
            continue;
        Model model;
        if (!codec->parse(text, model, nullptr) || model.nodes.isEmpty())
            continue;
        Board scratch;
        scratch.setStyle(board_->style());
        buildBoard(scratch, *codec, model);
        if (family->autoArrange) {
            family->autoArrange(scratch);
        } else if (codec->arrangeWithGraphviz()) {
            graphviz::ArrangeOptions options;
            options.rankdir = model.meta.value(QStringLiteral("rankdir")).toString(QStringLiteral("TB"));
            options.engine = model.meta.value(QStringLiteral("engine")).toString(QStringLiteral("dot"));
            options.clusters = family->graphvizClusters;
            graphviz::autoArrange(scratch, options);
        }
        QJsonObject json = scratch.toJson();
        if (board_->isEmpty())
            board_->meta = model.meta;
        board_->pasteJson(json, viewCenter());
        fitView(true);
        return;
    }
    emit message(tr("Nothing to paste into this diagram"));
}

// ---------------------------------------------------------------------------
// Text editing

void Canvas::beginTextEdit(Shape *shape, Link *link, const TextEditSpec &spec)
{
    if (isEditingText())
        endTextEdit(true);
    finishMotion();
    // A shape just created with its editor: one step of history for both.
    editBase_ = board_->takeFoldedEditBase();
    if (editBase_.isEmpty())
        editBase_ = board_->snapshot();
    editSpec_ = TextEditSpec(); // no set() while the text is loaded
    editShape_ = shape ? shape->id : QString();
    editLink_ = link ? link->id : QString();
    editOriginal_ = spec.get ? spec.get() : QString();
    {
        const QSignalBlocker blocker(editor_);
        editor_->setPlainText(editOriginal_);
    }
    editSpec_ = spec;
    QPalette palette = editor_->palette();
    palette.setColor(QPalette::Base, Qt::transparent);
    palette.setColor(QPalette::Text, shape ? (parseColor(shape->customFill).isValid()
                                                  ? readableTextColor(parseColor(shape->customFill), spec.color)
                                                  : spec.color)
                                           : spec.color);
    editor_->setPalette(palette);
    editor_->setAutoFillBackground(false);
    editor_->setStyleSheet(QStringLiteral("QPlainTextEdit { background: transparent; border: none; }"));
    syncEditor();
    editor_->show();
    editor_->setFocus();
    editor_->selectAll();
    update();
}

void Canvas::syncEditor()
{
    if (!isEditingText() && !editSpec_.rect)
        return;
    if (!editSpec_.rect)
        return;
    const QRectF r = editSpec_.rect();
    if (r.isNull()) {
        endTextEdit(true);
        return;
    }
    QFont font = text::referenceFont(editSpec_.font);
    font.setPixelSize(std::max(4, int(std::round(editSpec_.font.px * camera_.scale))));
    editor_->setFont(font);
    // Line height and alignment of every block, as the shape draws them.
    const qreal lineHeight = editSpec_.lineHeight * camera_.scale;
    QTextCursor cursor(editor_->document());
    cursor.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setLineHeight(lineHeight, QTextBlockFormat::FixedHeight);
    format.setAlignment(editSpec_.align == Align::Center  ? Qt::AlignHCenter
                        : editSpec_.align == Align::Right ? Qt::AlignRight
                                                          : Qt::AlignLeft);
    {
        const QSignalBlocker blocker(editor_);
        cursor.mergeBlockFormat(format);
    }
    const QPointF topLeft = camera_.toScreen(r.topLeft());
    const int blocks = std::max(1, editor_->document()->lineCount());
    const qreal height = std::max(r.height() * camera_.scale, blocks * lineHeight);
    // A little room so that the caret never clips; the text stays where
    // the shape draws it.
    editor_->setGeometry(
        QRectF(topLeft.x() - 2, topLeft.y(), r.width() * camera_.scale + 4, height + 4).toAlignedRect());
}

void Canvas::keepVisible(const Shape &shape)
{
    // Zooms out only when the box no longer fits, then moves the view just
    // enough to show it.
    const qreal padding = 28;
    const qreal availableWidth = std::max<qreal>(1, width() - padding * 2);
    const qreal availableHeight = std::max<qreal>(1, height() - padding * 2);
    const qreal scale =
        std::max(board_->style().minScale, std::min({camera_.scale, availableWidth / std::max<qreal>(1, shape.w),
                                                     availableHeight / std::max<qreal>(1, shape.h)}));
    const QPointF center = camera_.toScreen(QPointF(shape.cx(), shape.cy()));
    camera_.scale = scale;
    camera_.x = center.x() - shape.cx() * scale;
    camera_.y = center.y() - shape.cy() * scale;
    const QRectF box(camera_.toScreen(shape.rect().topLeft()), camera_.toScreen(shape.rect().bottomRight()));
    if (box.left() < padding)
        camera_.x += padding - box.left();
    else if (box.right() > width() - padding)
        camera_.x += width() - padding - box.right();
    if (box.top() < padding)
        camera_.y += padding - box.top();
    else if (box.bottom() > height() - padding)
        camera_.y += height() - padding - box.bottom();
    emit zoomChanged(camera_.scale);
}

void Canvas::endTextEdit(bool commit)
{
    if (!isEditingText())
        return;
    const TextEditSpec spec = editSpec_;
    editSpec_ = TextEditSpec();
    if (spec.set)
        spec.set(commit ? editor_->toPlainText() : editOriginal_);
    if (Shape *s = board_->shape(editShape_)) {
        const QPointF center(s->cx(), s->cy());
        const QPointF origin(s->x, s->y);
        board_->measureShape(*s);
        if (s->editResizeAnchorTopLeft()) {
            s->x = origin.x();
            s->y = origin.y();
        } else {
            s->x = center.x() - s->w / 2;
            s->y = center.y() - s->h / 2;
        }
        board_->containShape(*s, true);
    }
    editor_->hide();
    editShape_.clear();
    editLink_.clear();
    const QByteArray base = editBase_;
    editBase_.clear();
    board_->resolveLabels();
    board_->pushHistory(base);
    board_->touch();
    setFocus();
}

bool Canvas::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == editor_) {
        if (event->type() == QEvent::ShortcutOverride && isTextEditingKey(static_cast<QKeyEvent *>(event))) {
            // Typing belongs to the editor, not to the window's shortcuts.
            event->accept();
            return false;
        }
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape) {
                endTextEdit(false);
                return true;
            }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                const bool modified = key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
                // Enter ends a one-line text, Ctrl+Enter a text of several;
                // otherwise a new line.
                const bool ends = editSpec_.enterCommits ? !modified : (key->modifiers() & Qt::ControlModifier);
                if (ends) {
                    endTextEdit(true);
                    return true;
                }
                editor_->insertPlainText(QStringLiteral("\n"));
                return true;
            }
        }
        if (event->type() == QEvent::FocusOut && isEditingText()) {
            // A context menu opened over the editor keeps the edit.
            if (static_cast<QFocusEvent *>(event)->reason() != Qt::PopupFocusReason)
                endTextEdit(true);
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Menus

void Canvas::chooseElement(QPointF screen, QPointF point, const QString &from)
{
    const Family *family = board_->family();
    if (!family)
        return;
    if (family->elements.isEmpty()) {
        createElement(ElementPreset{}, point, from);
        return;
    }
    QMenu menu(this);
    menu.setToolTipsVisible(true);
    const Style &style = board_->style();
    for (const ElementPreset &preset : family->elements) {
        QAction *action = menu.addAction(presetIcon(*family, preset, style), preset.label);
        if (!preset.help.isEmpty())
            action->setToolTip(preset.help);
        connect(action, &QAction::triggered, this, [this, preset, point, from] { createElement(preset, point, from); });
    }
    menu.exec(screen.toPoint());
}

void Canvas::createElement(const ElementPreset &preset, QPointF point, const QString &from)
{
    const Family *family = board_->family();
    if (preset.trace) {
        // A two-point element (an arrow): drawn by a press-drag-release.
        trace_ = Trace{preset.shapeType, preset.props};
        setCursor(Qt::CrossCursor);
        emit message(tr("Drag to draw it"));
        return;
    }
    QJsonObject props = preset.props;
    // Created inside a frame, it joins it.
    if (family && family->nestedCreation) {
        if (Shape *parent = board_->containerAt(point))
            props.insert(QStringLiteral("parentBoundary"), parent->key.isEmpty() ? parent->id : parent->key);
    }
    if (from.isEmpty()) {
        board_->createShapeAt(point, preset.shapeType, props, true);
        return;
    }
    // At the end of a link: the shape and its link are one step.
    const QByteArray before = board_->snapshot();
    Shape *created = nullptr;
    {
        const QSignalBlocker blocker(board_);
        QJsonObject all = board_->creationShapeProps;
        for (auto it = props.begin(); it != props.end(); ++it)
            all.insert(it.key(), it.value());
        created = board_->addShape(preset.shapeType, all);
        if (created) {
            created->x = point.x() - created->w / 2;
            created->y = point.y() - created->h / 2;
            board_->containShape(*created, true);
            board_->connect(from, created->id);
        }
    }
    if (!created)
        return;
    board_->selectShape(created->id);
    emit board_->elementsCreated({created->id}, {});
    board_->resolveLabels();
    board_->pushHistory(before);
}

void Canvas::chooseRelation(QPointF screen, const QString &from, const QString &to, const QJsonObject &portProps)
{
    const Family *family = board_->family();
    QMenu menu(this);
    menu.setToolTipsVisible(true);
    const Shape *a = board_->shape(from);
    const Shape *b = board_->shape(to);
    QAction *title = menu.addAction(
        QStringLiteral("%1 → %2").arg(a ? a->displayText() : QString(), b ? b->displayText() : QString()));
    title->setEnabled(false);
    menu.addSeparator();
    const QByteArray before = dragBase_.isEmpty() ? board_->snapshot() : dragBase_;
    for (const RelationPreset &preset : family->relations) {
        QAction *action = menu.addAction(relationIcon(*family, preset, board_->style()), preset.label);
        if (!preset.help.isEmpty())
            action->setToolTip(preset.help);
        connect(action, &QAction::triggered, this, [this, preset, from, to, portProps, before] {
            QJsonObject props = preset.props;
            for (auto it = portProps.begin(); it != portProps.end(); ++it)
                props.insert(it.key(), it.value());
            if (Link *l = board_->connect(from, to, props)) {
                emit board_->elementsCreated({}, {l->id});
                board_->selectLink(l->id);
            }
            board_->resolveLabels();
            board_->pushHistory(before);
        });
    }
    menu.exec(screen.toPoint());
}

void Canvas::addFillMenu(QMenu *menu)
{
    QMenu *fill = menu->addMenu(tr("Color"));
    for (const QString &color : swatches()) {
        QPixmap swatch(16, 16);
        swatch.fill(QColor::fromString(color));
        QAction *action = fill->addAction(QIcon(swatch), color);
        connect(action, &QAction::triggered, this, [this, color] { board_->paintSelection(color); });
    }
    fill->addSeparator();
    connect(fill->addAction(tr("Default Color")), &QAction::triggered, this, [this] { board_->paintSelection({}); });
}

void Canvas::addLinkStyleMenu(QMenu *menu)
{
    QMenu *line = menu->addMenu(tr("Line Style"));
    const QList<std::pair<QString, QString>> styles{{QString(), tr("Default")},
                                                    {QStringLiteral("solid"), tr("Solid")},
                                                    {QStringLiteral("dashed"), tr("Dashed")},
                                                    {QStringLiteral("dotted"), tr("Dotted")}};
    for (const auto &[value, label] : styles) {
        connect(line->addAction(label), &QAction::triggered, this,
                [this, value] { board_->styleSelectedLinks(QJsonObject{{QStringLiteral("customLineStyle"), value}}); });
    }
    QMenu *color = menu->addMenu(tr("Line Color"));
    for (const QString &value : lineColors()) {
        QPixmap swatch(16, 16);
        swatch.fill(QColor::fromString(value));
        connect(color->addAction(QIcon(swatch), value), &QAction::triggered, this,
                [this, value] { board_->styleSelectedLinks(QJsonObject{{QStringLiteral("customColor"), value}}); });
    }
    color->addSeparator();
    connect(color->addAction(tr("Default Color")), &QAction::triggered, this,
            [this] { board_->styleSelectedLinks(QJsonObject{{QStringLiteral("customColor"), QString()}}); });
    QMenu *width = menu->addMenu(tr("Line Width"));
    connect(width->addAction(tr("Default")), &QAction::triggered, this,
            [this] { board_->styleSelectedLinks(QJsonObject{{QStringLiteral("customWidth"), QJsonValue::Null}}); });
    for (int value : {1, 2, 3, 4, 6, 8}) {
        connect(width->addAction(QString::number(value)), &QAction::triggered, this,
                [this, value] { board_->styleSelectedLinks(QJsonObject{{QStringLiteral("customWidth"), value}}); });
    }
}

void Canvas::addPropertyMenus(QMenu *menu, const QList<Property> &properties, bool links)
{
    for (const Property &property : properties) {
        if (property.kind == Property::Kind::Choice) {
            QMenu *sub = menu->addMenu(property.label);
            QString current;
            if (links) {
                if (const Link *l = board_->selectedLink())
                    current = l->propertyValue(property.id).toString();
            } else if (const Shape *s = board_->selectedShape()) {
                current = s->propertyValue(property.id).toString();
            }
            for (const auto &[value, label] : property.choices) {
                QAction *action = sub->addAction(label);
                action->setCheckable(true);
                action->setChecked(value == current);
                const QString id = property.id;
                connect(action, &QAction::triggered, this, [this, id, value, links] {
                    board_->edit([&] {
                        if (links) {
                            for (const QString &linkId : board_->linkSelection()) {
                                if (Link *l = board_->link(linkId))
                                    l->setPropertyValue(id, value);
                            }
                        } else {
                            for (const QString &shapeId : board_->selection()) {
                                if (Shape *s = board_->shape(shapeId)) {
                                    s->setPropertyValue(id, value);
                                    board_->measureShape(*s);
                                }
                            }
                        }
                    });
                });
            }
        } else if (property.kind == Property::Kind::Bool) {
            QAction *action = menu->addAction(property.label);
            action->setCheckable(true);
            if (links) {
                if (const Link *l = board_->selectedLink())
                    action->setChecked(l->propertyValue(property.id).toBool());
            } else if (const Shape *s = board_->selectedShape()) {
                action->setChecked(s->propertyValue(property.id).toBool());
            }
            const QString id = property.id;
            connect(action, &QAction::toggled, this, [this, id, links](bool checked) {
                board_->edit([&] {
                    if (links) {
                        for (const QString &linkId : board_->linkSelection()) {
                            if (Link *l = board_->link(linkId))
                                l->setPropertyValue(id, checked);
                        }
                    } else {
                        for (const QString &shapeId : board_->selection()) {
                            if (Shape *s = board_->shape(shapeId)) {
                                s->setPropertyValue(id, checked);
                                board_->measureShape(*s);
                            }
                        }
                    }
                });
            });
        }
    }
}

void Canvas::showContextMenu(QPointF screen)
{
    const QPointF p = world(screen);
    QMenu menu(this);
    const Family *family = board_->family();
    Shape *s = shapeAt(p);
    Link *l = s ? nullptr : linkAt(p);
    if (s && !board_->selection().contains(s->id))
        board_->selectShape(s->id);
    if (l && !board_->linkSelection().contains(l->id))
        board_->selectLink(l->id);
    if (s) {
        connect(menu.addAction(tr("Edit Text")), &QAction::triggered, this, [this] { editSelection(); });
        if (board_->selection().size() == 1)
            addPropertyMenus(&menu, s->properties(), false);
        addFillMenu(&menu);
        menu.addSeparator();
        connect(menu.addAction(tr("Cut")), &QAction::triggered, this, &Canvas::cut);
        connect(menu.addAction(tr("Copy")), &QAction::triggered, this, &Canvas::copy);
        connect(menu.addAction(tr("Duplicate")), &QAction::triggered, this, [this] { board_->duplicateSelection(); });
        menu.addSeparator();
        QAction *remove = menu.addAction(tr("Delete"));
        remove->setEnabled(board_->allowDeletion());
        connect(remove, &QAction::triggered, this, &Canvas::deleteSelection);
    } else if (l) {
        connect(menu.addAction(tr("Edit Label")), &QAction::triggered, this, [this] { editSelection(); });
        if (board_->linkSelection().size() == 1)
            addPropertyMenus(&menu, l->properties(), true);
        addLinkStyleMenu(&menu);
        menu.addSeparator();
        QAction *remove = menu.addAction(tr("Delete"));
        remove->setEnabled(board_->allowDeletion());
        connect(remove, &QAction::triggered, this, &Canvas::deleteSelection);
    } else {
        if (family && !family->elements.isEmpty()) {
            QMenu *add = menu.addMenu(tr("Add"));
            for (const ElementPreset &preset : family->elements) {
                connect(add->addAction(presetIcon(*family, preset, board_->style()), preset.label), &QAction::triggered,
                        this, [this, preset, p] { createElement(preset, p, {}); });
            }
        } else if (family) {
            connect(menu.addAction(tr("Add")), &QAction::triggered, this,
                    [this, p] { createElement(ElementPreset{}, p, {}); });
        }
        connect(menu.addAction(tr("Paste")), &QAction::triggered, this, &Canvas::paste);
        connect(menu.addAction(tr("Select All")), &QAction::triggered, this, [this] { board_->selectAll(); });
        menu.addSeparator();
        connect(menu.addAction(tr("Lay Out")), &QAction::triggered, this, [this] { arrange(true); });
        connect(menu.addAction(tr("Fit")), &QAction::triggered, this, [this] { fitView(true); });
    }
    menu.exec(mapToGlobal(screen.toPoint()));
}

// ---------------------------------------------------------------------------

void Canvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (needsFit_ && width() > 1 && height() > 1) {
        needsFit_ = false;
        fitView(false);
    } else if (!userCamera_ && !board_->isEmpty()) {
        // Until the user takes the camera, the view frames itself again.
        fitView(false);
    }
    syncEditor();
}

void Canvas::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (needsFit_ && width() > 1 && height() > 1) {
        needsFit_ = false;
        fitView(false);
    }
}

} // namespace diagram
