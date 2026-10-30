// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/ThreeWayMerge.h"

#include <QByteArray>
#include <QByteArrayList>
#include <QList>
#include <QString>

#include <utility>

// The resolution of one conflicted text file, as the merge view builds it:
// the file's stretches (ThreeWayMerge), each with what the result takes
// from it. A stretch only one side changed takes that side's lines; a
// conflict is open until a side, both, or the user's own lines are chosen.
// An open conflict may have a proposal from the agent (MergeProposals),
// shown in its place until it is accepted or rejected. Every change can be
// undone. No widget: the view shows it and edits it.
class MergeDocument {
public:
    // What the result takes from a stretch.
    enum class Choice {
        Unresolved,          // a conflict not decided yet: its proposal's lines, or the ancestor's, meanwhile
        Current,             // the current side's lines
        Incoming,            // the incoming side's
        CurrentThenIncoming, // both, the current side's first
        IncomingThenCurrent, // both, the incoming side's first
        Edited,              // lines the user wrote (replaceResultLines())
        Combined,            // each side's changes of different words (SimpleConflicts)
        Suggested,           // the lines of a proposal accepted
    };

    struct Segment {
        ThreeWayMerge::Chunk chunk;
        Choice choice = Choice::Unresolved;
        QByteArrayList edited; // the lines of Edited, Combined and Suggested

        bool operator==(const Segment &) const = default;
    };

    // Lines the agent proposes for a conflict, and why.
    struct Proposal {
        QByteArrayList lines;
        QString reason;

        bool operator==(const Proposal &) const = default;
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

    // Takes `choice` for `segment`; not Edited, Combined or Suggested, which
    // replaceResultLines(), resolveSimpleConflicts() and acceptProposal()
    // give.
    void choose(int segment, Choice choice);
    // Takes `choice` for every conflict still open, as one change.
    void resolveRemaining(Choice choice);
    // Resolves the open conflicts that need no one to decide
    // (SimpleConflicts), as one change; returns how many.
    int resolveSimpleConflicts();
    // Replaces `count` lines of the result from `first` by `lines`, as the
    // user types them: the stretches the lines belong to become one, with
    // the lines the user wrote. An insertion between two stretches goes to
    // an open conflict without lines there, else to the stretch after it.
    // The replacements that follow one another are undone together, until
    // finishTyping() or another change.
    void replaceResultLines(int first, int count, const QByteArrayList &lines);

    void finishTyping() { isTyping_ = false; }

    // Proposes `proposal` for the conflict `conflict`: shown while a segment
    // with that stretch is open. Not a change: nothing to undo. A conflict
    // the user has since joined with others never shows it.
    void propose(const ThreeWayMerge::Chunk &conflict, const Proposal &proposal);
    // The proposal shown at `segment`, if it is open and has one.
    const Proposal *proposalFor(int segment) const;
    // Takes the proposal shown at `segment` (a change), or forgets it.
    void acceptProposal(int segment);
    void rejectProposal(int segment);
    // Takes every proposal shown, as one change; returns how many.
    int acceptAllProposals();

    bool canUndo() const { return !history_.isEmpty(); }

    bool canRedo() const { return !undone_.isEmpty(); }

    void undo();
    void redo();

private:
    // The lines the result shows for `segment`.
    QByteArrayList shownLines(const Segment &segment) const;
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
    // The last change was a replacement: the next one joins it.
    bool isTyping_ = false;
    // By the conflict's stretch, which stays the same as long as the
    // segment is not joined with others.
    QList<std::pair<ThreeWayMerge::Chunk, Proposal>> proposals_;
};
