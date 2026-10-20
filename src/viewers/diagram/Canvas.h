// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Shape.h"

#include <QElapsedTimer>
#include <QHash>
#include <QIcon>
#include <QPointer>
#include <QSet>
#include <QWidget>

#include <memory>
#include <optional>
#include <vector>

class QKeyEvent;
class QMenu;
class QPlainTextEdit;
class QTimer;

// The drawing surface of a diagram, ported from sidecar's ShapeBoard (its
// rendering and pointer interaction) on QPainter:
// - double-click: edit an element's text or a link's label, or add an
//   element (a menu of the family's elements);
// - drag a shape to move it (into or out of a frame, aligned on the others
//   where the family wants it, Alt to turn that off), its corner handle to
//   resize it, one of its ports to link it (released on a shape: a link, or a
//   menu of the family's relations; in empty space: a new linked element);
// - drag in empty space or with the middle button to pan, Shift+drag to
//   select a rectangle; wheel to zoom;
// - right-click for the context menus.
namespace diagram {

class Board;
struct ElementPreset;

namespace document {
struct Loaded;
}

namespace graphviz {
struct ArrangeOptions;
}
struct Family;
struct RelationPreset;

struct Camera {
    qreal x = 0;
    qreal y = 0;
    qreal scale = 1;

    QPointF toScreen(QPointF world) const { return {world.x() * scale + x, world.y() * scale + y}; }

    QPointF toWorld(QPointF screen) const { return {(screen.x() - x) / scale, (screen.y() - y) / scale}; }
};

class Canvas : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Canvas)

public:
    explicit Canvas(Board *board, QWidget *parent = nullptr);
    ~Canvas() override;

    Board *board() const { return board_; }

    void setGridVisible(bool visible);

    bool isGridVisible() const { return gridVisible_; }

    qreal zoom() const { return camera_.scale; }

    const Camera &camera() const { return camera_; }

    // The world point at the center of the view.
    QPointF viewCenter() const;
    bool isEditingText() const;

    // Frames the whole diagram (at most 125 %).
    void fitView(bool animate);

    // Fits the view once the canvas has its size (after loading a file).
    void fitWhenShown() { needsFit_ = true; }

    void zoomBy(qreal factor);
    void resetZoom();
    // Lays the diagram out (Graphviz, or the family's own layout), animated,
    // then frames it. False, with a message, if the layout failed.
    bool arrange(bool animate = true);
    // Computes the links' routes alone, nodes pinned (after a move).
    void routeLinksSoon();
    // Lays out, places or routes what a file or a source just read needs.
    void settle(const document::Loaded &loaded, bool animate);
    // Ends animations: the model is already at its final state.
    void finishMotion();

    void copy();
    void cut();
    void paste();
    void deleteSelection();
    void editSelection();

    // Watches the in-place text editor: its keys and its loss of focus.
    bool eventFilter(QObject *watched, QEvent *event) override;

    // Draws the board in world coordinates (no grid, no selection): image
    // and SVG exports.
    static void paintBoard(QPainter &painter, const Board &board);
    // An icon of a family's element or relation, in `style`.
    static QIcon presetIcon(const Family &family, const ElementPreset &preset, const Style &style);
    static QIcon relationIcon(const Family &family, const RelationPreset &preset, const Style &style);

signals:
    void zoomChanged(qreal zoom);
    void message(const QString &text);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    enum class Drag {
        None,
        Pan,
        Move,
        Resize,
        Connect,
        Rubber,
        Trace,
        RightPending
    };

    struct Guide {
        bool vertical;
        qreal value, from, to;
    };

    struct Appear {
        qreal start; // ms on the animation clock
        qreal duration;
    };

    // A removed element, drawn fading out.
    struct Ghost {
        std::shared_ptr<Shape> shape;
        std::vector<QPointF> line;
        QColor color;
        qreal start;
    };

    // Which selection a property menu reads and edits.
    enum class PropertyTarget {
        Shapes,
        Links
    };

    // The shapes in their drawing order.
    struct Layers {
        std::vector<const Shape *> background; // containers, behind the links
        std::vector<const Shape *> foreground;
    };

    QPointF world(QPointF screen) const { return camera_.toWorld(screen); }

    // Reactions to the editor and the board (Canvas.cpp).
    void onEditorTextChanged();
    void fadeOutRemoved(const QList<const Shape *> &shapes, const QList<const Link *> &links);

    // Painting (CanvasPainting.cpp).
    void render(QPainter &painter, const QRectF &area);
    void drawScene(QPainter &painter) const;
    Layers layers() const;
    void drawShape(QPainter &painter, const Shape &shape, qreal now) const;
    // The selection halo, the ports and the resize handle of `shape`.
    void drawHandles(QPainter &painter, const Shape &shape) const;
    // The link or the arrow being dragged out.
    void drawConnectPreview(QPainter &painter) const;
    void drawTracePreview(QPainter &painter) const;
    void drawGhosts(QPainter &painter, qreal now) const;
    void drawGuides(QPainter &painter) const;
    void drawLinkOverlays(QPainter &painter) const;
    void drawRubberBand(QPainter &painter) const;
    void drawGrid(QPainter &painter, const QRectF &area) const;
    void drawSelectionHalo(QPainter &painter, const Shape &shape) const;
    void drawResizeHandle(QPainter &painter, const Shape &shape) const;
    void drawPorts(QPainter &painter, const Shape &shape) const;
    bool portsVisible(const Shape &shape) const;

    // Hit tests, in world coordinates (CanvasInput.cpp).
    Shape *shapeAt(QPointF point) const;
    Shape *connectionShapeAt(QPointF point) const;
    Link *linkAt(QPointF point) const;
    std::optional<std::pair<Shape *, Port>> portAt(QPointF point) const;
    Shape *resizeHandleAt(QPointF point) const;
    void updateHover(QPointF screen);

    // Pointer gestures (CanvasInput.cpp).
    void pressLeft(QPointF point, Qt::KeyboardModifiers modifiers);
    void startResize(Shape &shape);
    void startMove(QPointF point);
    // The contents of the selected frames, which follow them.
    QSet<QString> carriedBySelection() const;
    QSet<QString> draggedIds() const;
    void moveDragged(QPointF point, bool snap);
    void placeDragged(QPointF point);
    void snapDragged();
    QString dropTargetOfDragged() const;
    void containDragged();
    void finishTrace(QPointF point);
    void finishMove();
    void reparentDragged();
    void finishConnect(QPointF screen);

    // Keyboard (CanvasInput.cpp).
    bool runControlShortcut(int key, Qt::KeyboardModifiers modifiers);
    bool runEditingKey(const QKeyEvent &event, Qt::KeyboardModifiers modifiers);
    void nudgeSelection(QPointF delta);
    QSet<QString> nudgedShapes() const;

    // View, layout and animations (Canvas.cpp).
    graphviz::ArrangeOptions arrangeOptions() const;
    void zoomAt(QPointF screen, qreal factor);
    void animateCamera(const Camera &to);
    void startAnimation();
    void stepAnimation();
    // The positions before a change, to slide from.
    QHash<QString, QRectF> captureGeometry() const;
    void animateGeometry(const QHash<QString, QRectF> &before, const Camera *cameraTo = nullptr);
    // Paints with the shapes where the animation shows them
    // (CanvasPainting.cpp).
    template <typename Fn>
    void withVisualGeometry(Fn draw);

    // In-place text editing (Canvas.cpp).
    void beginTextEdit(Shape *shape, Link *link, const TextEditSpec &spec);
    void endTextEdit(bool commit);
    // Escape and Enter in the editor; false for the keys it types.
    bool handleEditorKey(const QKeyEvent &key);
    void syncEditor();
    void keepVisible(const Shape &shape);

    // Menus (CanvasMenus.cpp).
    void showContextMenu(QPointF screen);
    void addShapeActions(QMenu &menu, const Shape &shape);
    void addLinkActions(QMenu &menu, const Link &link);
    void addCanvasActions(QMenu &menu, QPointF point);
    void addFillMenu(QMenu *menu);
    void addLinkStyleMenu(QMenu *menu);
    void addPropertyMenus(QMenu *menu, const QList<Property> &properties, PropertyTarget target);
    void addChoiceMenu(QMenu *menu, const Property &property, PropertyTarget target);
    void addToggleAction(QMenu *menu, const Property &property, PropertyTarget target);
    QVariant selectedPropertyValue(const QString &id, PropertyTarget target) const;
    void setSelectionProperty(const QString &id, const QVariant &value, PropertyTarget target);
    // Asks which element to add at `point`, linked from `from` if given.
    void chooseElement(QPointF screen, QPointF point, const QString &from);
    // Asks which relation joins `from` to `to`.
    void chooseRelation(QPointF screen, const QString &from, const QString &to, const QJsonObject &portProps);
    void createElement(const ElementPreset &preset, QPointF point, const QString &from);
    void insertSource(const QString &text);

    Board *board_;
    Camera camera_;
    bool gridVisible_ = true;
    bool needsFit_ = false;
    bool userCamera_ = false;
    bool spaceDown_ = false;

    // The gesture in progress.
    Drag drag_ = Drag::None;
    QPointF dragStart_; // world
    QPointF dragStartScreen_;
    Camera panStart_;
    bool moved_ = false;
    QByteArray dragBase_;
    std::vector<std::pair<QString, QPointF>> dragOffsets_; // id, pointer - origin
    QStringList dragRoots_;
    QString dragShape_;
    Port dragPort_;
    qreal resizeWidth_ = 0, resizeHeight_ = 0;
    QString dropTarget_;
    std::vector<Guide> guides_;
    QPointF pointer_; // world

    struct Trace {
        QString type;
        QJsonObject props;
    };

    std::optional<Trace> trace_;

    QString hoverShape_;
    QString hoverConnectionShape_;

    // In-place text editing.
    QPlainTextEdit *editor_;
    TextEditSpec editSpec_;
    QString editShape_;
    QString editLink_;
    QString editOriginal_;
    QByteArray editBase_;

    // Animations.
    QTimer *animationTimer_;
    QElapsedTimer clock_;
    QHash<QString, QRectF> geometryFrom_;
    QHash<QString, QRectF> geometryTo_;
    QHash<QString, Spline> splinesFrom_;
    qreal geometryStart_ = -1;
    bool cameraAnimating_ = false;
    Camera cameraFrom_, cameraTo_;
    qreal cameraStart_ = 0;
    QHash<QString, Appear> appearing_;
    std::vector<Ghost> ghosts_;
    QTimer *routeTimer_;
    // A full layout keeps dot's curves: no routing of its own after it.
    bool arranging_ = false;
};

} // namespace diagram
