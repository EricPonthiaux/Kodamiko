#pragma once

#include "viewers/diagram/Shape.h"

namespace diagram {
struct Model;
}

// Mind maps, ported from sidecar's mindmap-diagram.js: presented the way
// Mermaid presents one, but on the board's own palette instead of Mermaid's
// branch colors:
//   depth 0 (root)  solid pill, white bold text: the center of gravity
//   depth 1         rounded bubbles, regular ink
//   depth 2+        no box: muted text over a fine underline (Mermaid's leaves)
// Branches are smooth arrow-less curves; the visual weight decreases with
// depth.
namespace diagram::mindmap {

// The presentation level of a depth: 0, 1, or 2 for every deeper node.
int level(int depth);

class MindNode : public Shape {
public:
    MindNode();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    void measure(const Board &board) override;
    void resizeTo(qreal width, qreal height, const Board &board) override;
    void onDoubleClick(Board &board) override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;

    QString displayText() const override { return text; }

    QString text;
    // Drives the presentation; recomputed from the branches (see the family's
    // resolveLabels) so that a node keeps the look its place in the tree gives
    // it when the source is read again.
    int depth = 1;

private:
    // The bubble (or a leaf's underline) and its selection outline.
    void drawBody(QPainter &painter, const Style &style, bool selected) const;
    void drawLeafBody(QPainter &painter, const Style &style, bool selected) const;

    QStringList lines_;
};

// A branch: a gentle arrow-less curve, thicker close to the root.
class MindLink : public Link {
public:
    MindLink();

    QStringList graphvizAttributes(const Board &board) const override;
    LinkStyle drawStyle(const Board &board, bool selected) const override;
    void draw(QPainter &painter, const Board &board, bool selected) const override;

private:
    qreal nativeWidth(const Board &board) const;
};

// Reads a Mermaid mindmap's tree (fence/mermaid-mindmap.js `parse`): the
// hierarchy is the leading-space count of each line. Nodes are "mind" nodes
// with `text` and `depth`, keyed by their source id or, for a bare text, by
// a slug of it; links go from parent to child. The product vision reads its
// skeleton through it.
void parseTree(const QString &source, Model &model);

} // namespace diagram::mindmap
