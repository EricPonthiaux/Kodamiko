#pragma once

#include "Shape.h"

#include <QElapsedTimer>
#include <QHash>
#include <QIcon>
#include <QPointer>
#include <QWidget>

#include <memory>
#include <optional>
#include <vector>

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
    bool eventFilter(QObject *watched, QEvent *event) override;

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

    QPointF world(QPointF screen) const { return camera_.toWorld(screen); }

    void render(QPainter &painter, const QRectF &area);
    void drawGrid(QPainter &painter, const QRectF &area) const;
    void drawSelectionHalo(QPainter &painter, const Shape &shape) const;
    void drawResizeHandle(QPainter &painter, const Shape &shape) const;
    void drawPorts(QPainter &painter, const Shape &shape) const;
    bool portsVisible(const Shape &shape) const;

    // Hit tests, in world coordinates.
    Shape *shapeAt(QPointF point) const;
    Shape *connectionShapeAt(QPointF point) const;
    Link *linkAt(QPointF point) const;
    std::optional<std::pair<Shape *, Port>> portAt(QPointF point) const;
    Shape *resizeHandleAt(QPointF point) const;
    void updateHover(QPointF screen);

    graphviz::ArrangeOptions arrangeOptions() const;
    void moveDragged(QPointF point, bool snap);
    void finishConnect(QPointF screen);
    void zoomAt(QPointF screen, qreal factor);
    void animateCamera(const Camera &to);
    void startAnimation();
    void stepAnimation();
    // The positions before a change, to slide from.
    QHash<QString, QRectF> captureGeometry() const;
    void animateGeometry(const QHash<QString, QRectF> &before, const Camera *cameraTo = nullptr);
    // Paints with the shapes where the animation shows them.
    template <typename Fn>
    void withVisualGeometry(Fn draw);

    void beginTextEdit(Shape *shape, Link *link, const TextEditSpec &spec);
    void endTextEdit(bool commit);
    void syncEditor();
    void keepVisible(const Shape &shape);

    void showContextMenu(QPointF screen);
    void addFillMenu(QMenu *menu);
    void addLinkStyleMenu(QMenu *menu);
    void addPropertyMenus(QMenu *menu, const QList<Property> &properties, bool links);
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
