// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Graphviz.h"
#include "Shape.h"

#include <QHash>
#include <QSizeF>
#include <QString>

#include <utility>
#include <vector>

// The DOT of a full layout (autoArrange): header, frames as clusters, nodes,
// edges and the invisible edges that stack disconnected pieces.
namespace diagram::graphviz {

// The frames given to Graphviz as clusters, and the shapes it lays out as
// nodes.
struct ClusterSet {
    std::vector<Shape *> boundaries;
    QHash<QString, Shape *> byId;
    QHash<QString, Shape *> byReference;
    std::vector<Shape *> ordinaryShapes;

    [[nodiscard]] Shape *parentOf(const Shape *s) const { return byReference.value(s->parentBoundary); }
};

// What the read-back needs to recognize each laid-out link.
struct LaidOutLinks {
    std::vector<Link *> inOrder;
    QHash<QString, Link *> byId;
    QHash<QString, std::pair<QString, QString>> endpointsById;
};

// The DOT of a full layout, and what reading its answer back needs.
struct ArrangeDot {
    QString text;
    ClusterSet clusters;
    LaidOutLinks links;
};

// Writes the full layout of `board`; `options` has its rankdir and engine
// set. Settles the frames' content on the way: a parent inferred from
// geometry, a parenthood loop cut.
ArrangeDot writeArrangeDot(const Board &board,
                           const ArrangeOptions &options,
                           const QHash<QString, QSizeF> &layoutSizes);

} // namespace diagram::graphviz
