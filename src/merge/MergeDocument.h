// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/ThreeWayMerge.h"

#include <QByteArray>
#include <QByteArrayList>
#include <QList>

#include <utility>

// The resolution of one conflicted text file, as the merge view builds it:
// the file's stretches (ThreeWayMerge), each with what the result takes
// from it. A stretch only one side changed takes that side's lines; a
// conflict is open until a side, both, or the user's own lines are chosen.
// Every change can be undone. No widget: the view shows it and edits it.
class MergeDocument {
public:
    // What the result takes from a stretch.
    enum class Choice {
        Unresolved,          // a conflict not decided yet: the ancestor's lines meanwhile
        Current,             // the current side's lines
        Incoming,            // the incoming side's
        CurrentThenIncoming, // both, the current side's first
        IncomingThenCurrent, // both, the incoming side's first
        Edited,              // lines the user wrote (replaceResultLines())
    };

    struct Segment {
        ThreeWayMerge::Chunk chunk;
        Choice choice = Choice::Unresolved;
        QByteArrayList edited; // the lines of Choice::Edited

        bool operator==(const Segment &) const = default;
    };

    // Lines of the result, from 0.
    struct LineRange {
        int first = 0;
        int count = 0;

        bool operator==(const LineRange &) const = default;
    };

    explicit MergeDocument(const QList<ThreeWayMerge::Chunk> &chunks);

    const QList<Segment> &segments() const { return segments_; }

    // The lines the result takes from `segment`, and where they are in it.
    QByteArrayList resultLines(int segment) const;
    LineRange resultRange(int segment) const;
    QByteArray resultText() const;

    // Whether no conflict is left open: the result can be written.
    bool isResolved() const;
    int unresolvedCount() const;

    // Takes `choice` for `segment`; not Choice::Edited, which only
    // replaceResultLines() gives.
    void choose(int segment, Choice choice);
    // Takes `choice` for every conflict still open, as one change.
    void resolveRemaining(Choice choice);
    // Replaces `count` lines of the result from `first` by `lines`, as the
    // user types them: the stretches the lines belong to become one, with
    // the lines the user wrote. An insertion between two stretches goes to
    // an open conflict without lines there, else to the stretch after it.
    void replaceResultLines(int first, int count, const QByteArrayList &lines);

    bool canUndo() const { return !history_.isEmpty(); }

    bool canRedo() const { return !undone_.isEmpty(); }

    void undo();
    void redo();

private:
    void remember();
    QList<LineRange> resultRanges() const;
    // The segments the replacement of `count` lines from `first` touches:
    // [first, last].
    std::pair<int, int> touchedSegments(int first, int count) const;

    QList<Segment> segments_;
    // The segments before each change, the last change last; and those of
    // the changes undone since, the last one undone last.
    QList<QList<Segment>> history_;
    QList<QList<Segment>> undone_;
};
