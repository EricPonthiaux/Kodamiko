// SPDX-License-Identifier: GPL-3.0-only

#include "Canvas.h"

#include "Board.h"
#include "CanvasMotion.h"
#include "Document.h"
#include "Family.h"
#include "Graphviz.h"
#include "ui/Motion.h"

#include <QApplication>
#include <QClipboard>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace diagram {

namespace {

using motion::clamp;
using motion::easeOut;

const char *const selectionMime = "application/x-kodamiko-diagram";

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

// A text being edited remeasures its box, which stays anchored where it
// was: its links do not jump at every character.
void remeasureAnchored(const Board &board, Shape &shape)
{
    const QPointF origin(shape.x, shape.y);
    const QPointF center(shape.cx(), shape.cy());
    board.measureShape(shape);
    if (shape.editResizeAnchorTopLeft()) {
        shape.x = origin.x();
        shape.y = origin.y();
    } else {
        shape.x = center.x() - shape.w / 2;
        shape.y = center.y() - shape.h / 2;
    }
}

// The editor's text color: readable on the shape's own fill, if it has one.
QColor editedTextColor(const Shape *shape, const QColor &defaultColor)
{
    if (!shape)
        return defaultColor;
    const QColor fill = parseColor(shape->customFill);
    return fill.isValid() ? readableTextColor(fill, defaultColor) : defaultColor;
}

Qt::Alignment qtAlignment(Align align)
{
    switch (align) {
    case Align::Left:
        return Qt::AlignLeft;
    case Align::Center:
        return Qt::AlignHCenter;
    case Align::Right:
        return Qt::AlignRight;
    }
    return Qt::AlignLeft;
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
    connect(editor_, &QPlainTextEdit::textChanged, this, &Canvas::onEditorTextChanged);

    animationTimer_->setInterval(16);
    connect(animationTimer_, &QTimer::timeout, this, &Canvas::stepAnimation);
    connect(&Motion::policy(), &Motion::Policy::enabledChanged, this, [this](bool enabled) {
        if (!enabled)
            finishMotion();
    });
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
        if (!Motion::policy().isEnabled())
            return;
        for (const QString &id : shapes)
            appearing_.insert(id, {qreal(clock_.elapsed()), motion::appearMs});
        startAnimation();
    });
    connect(board_, &Board::removing, this, &Canvas::fadeOutRemoved);
    board_->setTextEditor(
        [this](Shape *shape, Link *link, const TextEditSpec &spec) { beginTextEdit(shape, link, spec); });
}

void Canvas::onEditorTextChanged()
{
    if (!editSpec_.set)
        return;
    editSpec_.set(editor_->toPlainText());
    if (Shape *s = board_->shape(editShape_)) {
        remeasureAnchored(*board_, *s);
        keepVisible(*s);
    }
    syncEditor();
    board_->touch();
}

void Canvas::fadeOutRemoved(const QList<const Shape *> &shapes, const QList<const Link *> &links)
{
    if (!Motion::policy().isEnabled() || !isVisible() || board_->shapes().size() >= 200)
        return;
    const qreal now = clock_.elapsed();
    for (const Shape *s : shapes) {
        if (const std::shared_ptr<Shape> copy = board_->instantiate(s->type, s->toJson()))
            ghosts_.push_back({copy, {}, {}, now});
    }
    for (const Link *l : links) {
        if (const std::optional<Route> r = l->route(*board_))
            ghosts_.push_back({nullptr, l->routePolyline(*r), board_->style().link.color, now});
    }
    startAnimation();
}

Canvas::~Canvas()
{
    if (board_)
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
    const qreal scale =
        std::min({(width() - inset * 2) / std::max<qreal>(1, box.width()),
                  (height() - inset * 2) / std::max<qreal>(1, box.height()), style.maxScale, style.fitMaxScale});
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
    if (!Motion::policy().isEnabled()) {
        finishMotion();
        return;
    }
    if (!animationTimer_->isActive())
        animationTimer_->start();
}

void Canvas::stepAnimation()
{
    const qreal now = clock_.elapsed();
    bool running = false;
    if (cameraAnimating_) {
        const qreal t = easeOut((now - cameraStart_) / motion::cameraMs);
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
        if (now - geometryStart_ >= motion::geometryMs) {
            geometryStart_ = -1;
            geometryFrom_.clear();
            geometryTo_.clear();
            splinesFrom_.clear();
        } else {
            running = true;
        }
    }
    for (auto it = appearing_.begin(); it != appearing_.end();) {
        if (now - it->start >= it->duration) {
            it = appearing_.erase(it);
        } else {
            running = true;
            ++it;
        }
    }
    ghosts_.erase(std::remove_if(ghosts_.begin(), ghosts_.end(),
                                 [now](const Ghost &g) { return now - g.start >= motion::ghostMs; }),
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
    appearing_.clear();
    ghosts_.clear();
    animationTimer_->stop();
    syncEditor();
    update();
}

QHash<QString, QRectF> Canvas::captureGeometry() const
{
    QHash<QString, QRectF> result;
    for (const auto &s : board_->shapes())
        result.insert(s->id, s->rect());
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
// Clipboard

void Canvas::copy()
{
    if (!board_->copySelection())
        return;
    const QJsonObject json = board_->selectionJson();
    auto *mime = new QMimeData;
    mime->setData(QString::fromLatin1(selectionMime), QJsonDocument(json).toJson(QJsonDocument::Compact));
    // The selection in the family's notation too, for a text editor or
    // Claude.
    if (const Codec *codec = board_->family() ? defaultCodec(*board_->family()) : nullptr) {
        Board selection;
        selection.setFamily(board_->family());
        selection.fromJson(json);
        selection.meta = board_->meta;
        mime->setText(codec->serialize(selection, board_->meta));
    }
    QApplication::clipboard()->setMimeData(mime);
    emit message(tr("Copied"));
}

void Canvas::cut()
{
    copy();
    deleteSelection();
}

void Canvas::paste()
{
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (mime && mime->hasFormat(QString::fromLatin1(selectionMime))) {
        const QJsonObject json = QJsonDocument::fromJson(mime->data(QString::fromLatin1(selectionMime))).object();
        if (board_->family() && json.value(QStringLiteral("family")).toString() == board_->family()->id) {
            const QPointF center =
                board_->hasClipboard() && board_->selection().isEmpty() ? viewCenter() : viewCenter() + QPointF(24, 24);
            board_->pasteJson(json, center);
            return;
        }
    }
    if (mime && mime->hasText()) {
        insertSource(mime->text());
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
        const QJsonObject json = scratch.toJson();
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
    palette.setColor(QPalette::Text, editedTextColor(shape, spec.color));
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
    format.setAlignment(qtAlignment(editSpec_.align));
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

void Canvas::revealShape(const QString &reference)
{
    const Shape *shape = board_->shapeByReference(reference);
    if (!shape)
        return;
    board_->selectShape(shape->id);
    keepVisible(*shape);
    update();
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
        remeasureAnchored(*board_, *s);
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
    if (watched != editor_)
        return QWidget::eventFilter(watched, event);
    if (event->type() == QEvent::ShortcutOverride && isTextEditingKey(static_cast<QKeyEvent *>(event))) {
        // Typing belongs to the editor, not to the window's shortcuts.
        event->accept();
        return false;
    }
    if (event->type() == QEvent::KeyPress && handleEditorKey(*static_cast<QKeyEvent *>(event)))
        return true;
    // A context menu opened over the editor keeps the edit.
    const bool isEditFocusLost = event->type() == QEvent::FocusOut && isEditingText() &&
                                 static_cast<QFocusEvent *>(event)->reason() != Qt::PopupFocusReason;
    if (isEditFocusLost)
        endTextEdit(true);
    return QWidget::eventFilter(watched, event);
}

bool Canvas::handleEditorKey(const QKeyEvent &key)
{
    if (key.key() == Qt::Key_Escape) {
        endTextEdit(false);
        return true;
    }
    if (key.key() != Qt::Key_Return && key.key() != Qt::Key_Enter)
        return false;
    // Enter ends a one-line text, Ctrl+Enter a text of several; otherwise
    // a new line.
    const bool isModified = key.modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
    const bool isControl = key.modifiers() & Qt::ControlModifier;
    const bool ends = editSpec_.enterCommits ? !isModified : isControl;
    if (ends)
        endTextEdit(true);
    else
        editor_->insertPlainText(QStringLiteral("\n"));
    return true;
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
