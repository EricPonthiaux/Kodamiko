// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class QPainter;

// Diagram families and the notations they are written in, ported from
// sidecar: a family is a dictionary of shape and link types with the
// behavior of the whole diagram (its layout, its creation presets); a codec
// (sidecar's FenceParser) reads a source — Mermaid, PlantUML, a JSON scene —
// into a plain model and writes the board back into the same notation.
namespace diagram {

class Board;
class Link;
class Shape;

struct ShapeType {
    QString name;
    std::function<std::unique_ptr<Shape>()> make;
};

struct LinkType {
    QString name;
    std::function<std::unique_ptr<Link>()> make;
    bool allowSelf = false;  // self-loops (a state's self-transition)
    bool allowMulti = false; // several links between the same shapes (messages)
};

// An element offered for creation (double-click in empty space).
struct ElementPreset {
    QString id;
    QString label;
    QString help;
    QString shapeType; // empty: the family's default
    QJsonObject props;
    // Created by a press-drag-release gesture (an arrow, a line).
    bool trace = false;
};

// A relation offered when a link is drawn between two shapes.
struct RelationPreset {
    QString id;
    QString label;
    QString help;
    QJsonObject props;
};

struct Family {
    QString id;    // "flowchart"
    QString title; // "Flowchart"
    QList<ShapeType> shapes;
    QList<LinkType> links;
    QString defaultShape;
    QString defaultLink;

    QList<ElementPreset> elements;
    QList<RelationPreset> relations;
    bool cardinalities = false;  // relations carry multiplicities (UML, ER)
    QStringList technologies;    // C4 relations' technologies
    bool nestedCreation = false; // elements created in a frame join it

    bool allowDeletion = true;
    bool alignmentGuides = false;
    bool reparentOnDrop = false;
    // Graphviz: frames as clusters, membership recovered from geometry,
    // disconnected groups stacked, the better of LR and TB, spline mode.
    bool graphvizClusters = false;
    bool inferClusterMembership = false;
    bool graphvizStackDisconnected = false;
    bool graphvizAdaptiveDirection = false;
    QString graphvizSplines;
    // Laid out again on every load (its layout belongs to the source).
    bool alwaysArrange = false;
    // Routes recomputed alone after a move.
    bool routeLinksOnMove = true;
    // Whether the direction (rankdir) is a document setting.
    bool hasDirection = false;

    // A layout of its own instead of Graphviz (sequence lanes).
    std::function<void(Board &)> autoArrange;
    // Its layout can place newcomers on a copy without moving saved nodes.
    bool preservePlacedShapes = false;
    // Drawn under everything (sequence fragments).
    std::function<void(QPainter &, const Board &)> drawUnderlay;
    // Structural labels that must stay legible over lifelines and shapes.
    std::function<void(QPainter &, const Board &)> drawOverlay;
    // Labels that depend on the whole diagram (state transitions).
    std::function<void(Board &)> resolveLabels;
};

// ---------------------------------------------------------------------------
// Codecs

// What a source says, before it is a board: nodes and links keyed by their
// source ids, and the diagram's meta (rankdir, engine, title…).
struct Model {
    struct Node {
        QString type;
        QString key;
        QJsonObject props;
    };

    struct Edge {
        QString type;
        QString from; // node keys
        QString to;
        QJsonObject props;
    };

    QList<Node> nodes;
    QList<Edge> links;
    QJsonObject meta;
    // Statements read but not understood, for whoever wrote the source.
    QStringList warnings;
};

class Codec {
public:
    virtual ~Codec() = default;

    virtual QString id() const = 0;       // "mermaid-flow"
    virtual QString language() const = 0; // "mermaid", "plantuml", "board", "flowboard"
    virtual QString title() const = 0;    // "Flowchart"
    virtual const Family &family() const = 0;

    // Laid out by Graphviz (rather than the family's own layout or the
    // source's coordinates).
    virtual bool arrangeWithGraphviz() const { return true; }

    // The source carries the coordinates (a JSON scene): no layout to keep
    // beside it.
    virtual bool sourceOwnsLayout() const { return false; }

    // The notation writes the shapes' links to the code (Shape::codeLink):
    // the inspector offers to edit them.
    virtual bool writesCodeLinks() const { return false; }

    // Whether this codec reads `source` (its header).
    virtual bool matches(const QString &source) const = 0;
    // False, with `error`, if the source cannot be read.
    virtual bool parse(const QString &source, Model &model, QString *error) const = 0;
    virtual QString serialize(const Board &board, const QJsonObject &meta) const = 0;
    // A new, blank diagram in this notation.
    virtual QString blank() const = 0;
};

// Every family, in menu order.
const QList<const Family *> &allFamilies();
const Family *findFamily(const QString &id);
// Every codec, in detection order.
const QList<const Codec *> &allCodecs();
const Codec *findCodec(const QString &id);
// The codec of `language` that claims `source`.
const Codec *codecFor(const QString &language, const QString &source);
// The language of a file, from its extension ("mermaid" for .mmd); empty if
// the file is not a diagram.
QString languageOfFile(const QString &path);
// The codec a family's new files are written with.
const Codec *defaultCodec(const Family &family);

// Gives the nodes of `model` keyed in `codeLinks` their link to the code
// (the `code` prop); a key no node has is ignored.
void linkToCode(Model &model, const QHash<QString, QString> &codeLinks);

// Builds the board from a model: registers its family, creates the nodes
// (keyed by their source ids) on a provisional grid, then the links.
// Returns the meta.
QJsonObject buildBoard(Board &board, const Codec &codec, const Model &model);

} // namespace diagram
