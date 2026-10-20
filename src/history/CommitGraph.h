// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>
#include <QMetaType>

struct CommitSummary;

// A line of the graph within a commit's row, between two lanes: from the
// row's top edge to its middle (upper half), or from its middle to its
// bottom edge (lower half). `color` indexes a palette, by lane.
struct GraphSegment {
    int from = 0;
    int to = 0;
    int color = 0;

    bool operator==(const GraphSegment &) const = default;
};

// The graph in a commit's row: the commit's dot on its lane, and the lines
// passing through the row. The lower half of a row continues into the upper
// half of the next one.
struct GraphRow {
    int lane = 0;
    int color = 0;
    QList<GraphSegment> upper;
    QList<GraphSegment> lower;

    bool operator==(const GraphRow &) const = default;
};

Q_DECLARE_METATYPE(GraphRow)

namespace CommitGraph {

// The graph of `commits`, ordered children before parents (as CommitLog
// reads them), one row per commit: each line of history keeps a lane from
// a commit down to its first parent, a merge draws a line to each other
// parent, and lanes that reach the same commit join at its dot. A parent
// that is not in `commits` (beyond those read) leaves its line open at the
// bottom.
QList<GraphRow> layOut(const QList<CommitSummary> &commits);

// Lanes the rows need: one more than the rightmost lane they use.
int laneCount(const QList<GraphRow> &rows);

} // namespace CommitGraph
