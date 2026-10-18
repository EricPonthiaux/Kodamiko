#pragma once

#include "Shape.h"
#include "Style.h"

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>

#include <functional>
#include <memory>
#include <vector>

// The model of a diagram being edited, ported from sidecar's ShapeBoard
// without its DOM: the shapes and links of one diagram family, the
// selection, the containers' rules, the undo history and the clipboard.
// The canvas draws it and turns gestures into calls to it.
namespace diagram {

struct Family;

class Board : public QObject {
    Q_OBJECT

public:
    explicit Board(QObject *parent = nullptr);
    ~Board() override;

    // The family whose elements the board holds; it decides the shape and
    // link types and their defaults. Clears the board.
    void setFamily(const Family *family);

    const Family *family() const { return family_; }

    const Style &style() const { return style_; }

    void setStyle(const Style &style);

    // In paint order: the last shape is drawn on top.
    const std::vector<std::unique_ptr<Shape>> &shapes() const { return shapes_; }

    const std::vector<std::unique_ptr<Link>> &links() const { return links_; }

    Shape *shape(const QString &id) const;
    Link *link(const QString &id) const;
    // A shape by id, or else by key.
    Shape *shapeByReference(const QString &reference) const;
    QList<Shape *> childShapes(const Shape *parent) const;
    QList<Shape *> descendantShapes(const Shape *parent) const;

    bool isEmpty() const { return shapes_.empty(); }

    // --- Model
    // A shape of a registered type (empty: the family's default), measured.
    // Null if the type is unknown.
    Shape *addShape(const QString &type, const QJsonObject &props = {});
    // A link between two shapes; with a type that refuses several links
    // between the same shapes, the existing one. Null if refused.
    Link *addLink(const QString &type, const QString &from, const QString &to, const QJsonObject &props = {});
    // A shape of a registered type, not added to the board (previews).
    std::unique_ptr<Shape> instantiate(const QString &type, const QJsonObject &props) const;
    bool removeShape(const QString &id);
    bool removeLink(const QString &id);
    void bringToFront(const QString &id);
    void clear();
    bool allowDeletion() const;
    // A fresh id ("s12", "l3").
    QString newId(QChar prefix);

    void measureShape(Shape &shape, const QJsonObject *props = nullptr) const;
    void measureAll();

    // Keeps a contained shape inside its parent frame, growing the parent if
    // asked.
    void containShape(Shape &shape, bool growParent = false);
    // The smallest container under `point`, other than `moved` and its
    // descendants.
    Shape *containerAt(QPointF point, const Shape *moved = nullptr) const;
    // What dropping `shape` where it is would do to its membership: nothing
    // (nullopt), back to the top level (null), or the container adopting it.
    std::optional<Shape *> dropDecision(const Shape &shape, const QSet<QString> &moving) const;

    // --- Creation
    // Properties of the next shape and link created by gestures (a UML
    // classifier's stereotype, a relation's kind).
    QJsonObject creationShapeProps;
    QJsonObject creationLinkProps;
    // A shape centered on `center`, selected; `edit` opens its editor. One
    // step of history (with the first edit).
    Shape *createShapeAt(QPointF center, const QString &type = {}, const QJsonObject &props = {}, bool edit = true);
    // A link with the default type and the current relation preset.
    Link *connect(const QString &from, const QString &to, const QJsonObject &props = {});

    // --- Selection
    const QSet<QString> &selection() const { return selection_; }

    const QSet<QString> &linkSelection() const { return linkSelection_; }

    bool hasSelection() const { return !selection_.isEmpty() || !linkSelection_.isEmpty(); }

    void clearSelection();
    void selectShape(const QString &id, bool add = false);
    void selectLink(const QString &id, bool add = false);
    void deselect(const QString &id);
    void selectAll();
    void selectInRect(const QRectF &rect, bool add);
    // The single selected shape or link, or null.
    Shape *selectedShape() const;
    Link *selectedLink() const;

    // --- History
    // A stable fingerprint of the model (shapes and links sorted by id).
    // Without `routes`, the links' curves are left out: what tells whether
    // the diagram changed, routes being recomputed at will.
    QByteArray snapshot(bool routes = true) const;
    // Records a change: the snapshot taken before it, if the model moved.
    void pushHistory(const QByteArray &before);
    void applySnapshot(const QByteArray &data);

    bool canUndo() const { return !undo_.empty(); }

    bool canRedo() const { return !redo_.empty(); }

    void undo();
    void redo();
    void clearHistory();
    // Runs `change` as one step of history.
    void edit(const std::function<void()> &change);

    // --- Clipboard
    bool copySelection();
    bool cutSelection();
    bool pasteClipboard();

    bool hasClipboard() const { return !clipboardShapes_.isEmpty(); }

    // The selection as JSON (shapes, links), for the system clipboard.
    QJsonObject selectionJson() const;
    // Shapes and links from such JSON, pasted near `center`.
    bool pasteJson(const QJsonObject &json, QPointF center);
    void deleteSelection();
    void duplicateSelection();

    // --- Appearance
    // A custom fill on the selected shapes (empty: back to the native one).
    bool paintSelection(const QString &fill);
    // A sparse patch of customLineStyle / customColor / customWidth on the
    // selected links (null values reset).
    bool styleSelectedLinks(const QJsonObject &patch);

    // --- Serialization
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &json);
    // The source's meta (rankdir, engine, title…), kept for serialization.
    QJsonObject meta;

    // Everything drawn (shapes, lifelines, curves and labels), world units.
    QRectF contentBounds() const;

    // --- Text editing, done by the canvas.
    using TextEditor = std::function<void(Shape *, Link *, const TextEditSpec &)>;

    void setTextEditor(TextEditor editor) { textEditor_ = std::move(editor); }

    void beginTextEdit(Shape *shape, const TextEditSpec &spec);
    void beginTextEdit(Link *link, const TextEditSpec &spec);
    void editLinkLabel(Link &link);
    // The snapshot taken before a shape was created with its editor open:
    // the creation and the first edit are one step of history.
    QByteArray takeFoldedEditBase();

    // The family's label resolution (state transitions…), after changes.
    void resolveLabels();
    // The model changed outside of the history helpers (a gesture frame).
    void touch();

    // Whether gestures may move the board's contents (false while showing
    // an older version).
    quint64 version() const { return version_; }

signals:
    void changed();
    void selectionChanged();
    // A step of history was recorded or applied: the routes drawn against
    // the previous geometry are stale.
    void geometrySettled();
    // Shapes and links just created by a gesture, to animate.
    void elementsCreated(const QStringList &shapes, const QStringList &links);
    // Shapes and links about to be removed, to fade out.
    void removing(const QList<const Shape *> &shapes, const QList<const Link *> &links);

private:
    void reindex();
    std::unique_ptr<Shape> makeShape(const QString &type) const;
    std::unique_ptr<Link> makeLink(const QString &type, bool *allowSelf, bool *allowMulti) const;
    // The first link from `from` to `to`, or null.
    Link *linkBetween(const QString &from, const QString &to) const;
    // Removes every link touching the shape, and their selection.
    void removeLinksOf(const QString &shapeId);
    struct PastedShapes;
    void addClipboardContent(const QJsonArray &shapes, const QJsonArray &links, QPointF offset);
    PastedShapes pasteShapes(const QJsonArray &shapes, QPointF offset);
    void reparentPastedShapes(const PastedShapes &pasted);
    QStringList pasteLinks(const QJsonArray &links, const QHash<QString, QString> &idMap);

    const Family *family_ = nullptr;
    Style style_;
    std::vector<std::unique_ptr<Shape>> shapes_;
    std::vector<std::unique_ptr<Link>> links_;
    QHash<QString, Shape *> shapeIndex_;
    QHash<QString, Link *> linkIndex_;
    QSet<QString> selection_;
    QSet<QString> linkSelection_;
    std::vector<QByteArray> undo_;
    std::vector<QByteArray> redo_;
    QJsonArray clipboardShapes_;
    QJsonArray clipboardLinks_;
    qreal clipboardOffset_ = 24;
    TextEditor textEditor_;
    QByteArray foldedEditBase_;
    quint64 nextId_ = 1;
    quint64 version_ = 0;
};

} // namespace diagram
