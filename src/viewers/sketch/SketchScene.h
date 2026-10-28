// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>
#include <QPainterPath>
#include <QRectF>

// What DiagramSketch draws for a kind of diagram: blank shapes and the links
// between them, in a fixed space that DiagramSketch scales to its card, and
// the order the pen draws them in.

// The size of that space, in scene units.
constexpr qreal sketchSceneWidth = 480;
constexpr qreal sketchSceneHeight = 220;
// The height of the header of a Table, a Class and a Panel.
constexpr qreal sketchHeaderHeight = 20;

enum class SketchShape {
    Terminal,   // a pill: a start, an end, a mind map's topic
    Box,        // a rounded rectangle: a step, a state, a container
    Decision,   // a diamond
    StartDot,   // a filled dot: a state machine's start, a journey's score
    EndDot,     // a ring around a dot: a state machine's end
    Ellipse,    // a mind map's root
    Table,      // a header band over rows: a table, a titled card
    Class,      // a name, then attributes, then methods
    Person,     // a head over a body: a C4 person
    Boundary,   // a dashed frame around other shapes: a C4 system
    Cylinder,   // a database
    Lifeline,   // a head box over a dashed line down the scene
    Activation, // a thin bar on a lifeline
    Band,       // a filled header: a journey's section
    Panel,      // a numbered frame: an explainer board's scene
};

enum class SketchLinkEnd {
    None,
    Arrow,
    HollowTriangle, // inheritance
    CrowFoot,       // "many", in a data model
};

struct SketchNode {
    QRectF rect;
    SketchShape shape = SketchShape::Box;
    // Rows of a Table or Class compartment; the number of a Panel.
    int count = 0;
};

struct SketchLink {
    // From its start to the point its end touches.
    QPainterPath path;
    SketchLinkEnd end = SketchLinkEnd::Arrow;
    bool isDashed = false;
    // A blank label over its middle, once drawn.
    bool hasLabel = false;
};

// One thing the pen draws: nodes.at(index) or links.at(index).
struct SketchStroke {
    enum class Kind {
        Node,
        Link
    };
    Kind kind = Kind::Node;
    qsizetype index = 0;
};

struct SketchScene {
    QList<SketchNode> nodes;
    QList<SketchLink> links;
    QList<SketchStroke> strokes;
};
