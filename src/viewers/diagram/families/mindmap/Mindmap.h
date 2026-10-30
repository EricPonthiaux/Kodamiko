// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "viewers/diagram/Shape.h"

namespace diagram {
struct Model;
}

// Mind maps: a prominent center, tinted topics and open, underlined leaves.
// Each root branch has a theme-aware color inherited by its descendants.
// Curves have no arrows and become finer away from the center.
namespace diagram::mindmap {

// The presentation level of a depth: 0, 1, or 2 for every deeper node.
int level(int depth);
void arrange(Board &board);

class MindNode : public Shape {
public:
    MindNode();

    void load(const QJsonObject &props) override;
    QJsonObject toJson() const override;
    void measure(const Board &board) override;
    void resizeTo(qreal width, qreal height, const Board &board) override;
    void onDoubleClick(Board &board) override;
    void draw(QPainter &painter, const Board &board, const ShapeState &state) const override;
    QColor branchColor(const Style &style) const;

    QString displayText() const override { return text; }

    QString text;
    // Drives the presentation; recomputed from the branches (see the family's
    // resolveLabels) so that a node keeps the look its place in the tree gives
    // it when the source is read again.
    int depth = 1;
    // Derived from the tree, never serialized as a user color.
    int branchIndex = -1;

private:
    // The bubble (or a leaf's underline) and its selection outline.
    void drawBody(QPainter &painter, const Style &style, bool selected) const;
    void drawLeafBody(QPainter &painter, const Style &style, bool selected) const;
    QColor bodyColor(const Style &style) const;
    QColor inkColor(const Style &style) const;

    QStringList lines_;
};

// A branch: a gentle arrow-less curve, thicker close to the root.
class MindLink : public Link {
public:
    MindLink();

    QStringList graphvizAttributes(const Board &board) const override;
    std::optional<Route> route(const Board &board) const override;
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
