// SPDX-License-Identifier: GPL-3.0-only

#include "CommitGraph.h"

#include "git/CommitLog.h"

#include <QSet>

#include <algorithm>

namespace {

// A lane, from the row being laid out downwards: the commit its line goes
// to, empty when the lane is free.
struct Lane {
    QString expected;
    int color = 0;
};

struct Lanes {
    QList<Lane> lanes;
    int nextColor = 0;
};

int laneExpecting(const Lanes &state, const QString &id)
{
    for (int k = 0; k < state.lanes.size(); ++k) {
        if (state.lanes[k].expected == id)
            return k;
    }
    return -1;
}

// The first free lane, or a new one at the right, with a color of its own.
int takeFreeLane(Lanes &state, const QString &expected)
{
    int lane = laneExpecting(state, QString());
    if (lane < 0) {
        lane = int(state.lanes.size());
        state.lanes.append(Lane{});
    }
    state.lanes[lane] = {expected, state.nextColor++};
    return lane;
}

// The lines from the top of the row: straight down, or joining the dot of
// `commitId` at `lane` for those that expect it, which are freed.
QList<GraphSegment> upperHalf(Lanes &state, const QString &commitId, int lane)
{
    QList<GraphSegment> segments;
    for (int k = 0; k < state.lanes.size(); ++k) {
        Lane &current = state.lanes[k];
        if (current.expected.isEmpty())
            continue;
        const bool joinsTheDot = current.expected == commitId;
        segments.append({k, joinsTheDot ? lane : k, current.color});
        if (joinsTheDot && k != lane)
            current = {};
    }
    return segments;
}

// The lines down to the bottom of the row: the commit's own to its first
// parent, one to each other parent (a lane that already expects it, or a
// new one), and every other lane straight down.
QList<GraphSegment> lowerHalf(Lanes &state, const CommitSummary &commit, int lane)
{
    const QStringList &parents = commit.parentIds;
    state.lanes[lane].expected = parents.value(0);
    QList<GraphSegment> segments;
    QSet<int> startedHere;
    for (qsizetype p = 1; p < parents.size(); ++p) {
        int target = laneExpecting(state, parents[p]);
        if (target < 0) {
            target = takeFreeLane(state, parents[p]);
            startedHere.insert(target);
        }
        segments.append({lane, target, state.lanes[target].color});
    }
    for (int k = 0; k < state.lanes.size(); ++k) {
        if (!state.lanes[k].expected.isEmpty() && !startedHere.contains(k))
            segments.append({k, k, state.lanes[k].color});
    }
    while (!state.lanes.isEmpty() && state.lanes.last().expected.isEmpty())
        state.lanes.removeLast();
    return segments;
}

} // namespace

namespace CommitGraph {

QList<GraphRow> layOut(const QList<CommitSummary> &commits)
{
    QList<GraphRow> rows;
    rows.reserve(commits.size());
    Lanes state;
    for (const CommitSummary &commit : commits) {
        GraphRow row;
        row.lane = laneExpecting(state, commit.id);
        row.upper = upperHalf(state, commit.id, row.lane);
        // Nothing leads to a branch's tip: a new lane, from its dot down.
        if (row.lane < 0)
            row.lane = takeFreeLane(state, {});
        row.color = state.lanes[row.lane].color;
        row.lower = lowerHalf(state, commit, row.lane);
        rows.append(row);
    }
    return rows;
}

int laneCount(const QList<GraphRow> &rows)
{
    int count = 0;
    for (const GraphRow &row : rows) {
        count = std::max(count, row.lane + 1);
        for (const QList<GraphSegment> *half : {&row.upper, &row.lower}) {
            for (const GraphSegment &segment : *half)
                count = std::max({count, segment.from + 1, segment.to + 1});
        }
    }
    return count;
}

} // namespace CommitGraph
