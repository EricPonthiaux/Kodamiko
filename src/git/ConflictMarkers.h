// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>

#include <optional>

// The conflicts git writes into a file it could not merge, between markers
// of seven characters at the start of a line:
//
//     <<<<<<< HEAD            the current side (the branch checked out)
//     current lines
//     ||||||| base            only with merge.conflictStyle diff3 or zdiff3
//     lines of the common ancestor
//     =======
//     incoming lines
//     >>>>>>> other           the incoming side (the branch merged in)
//
// During a rebase, the current side is the branch rebased onto and the
// incoming side the commit replayed on it.
namespace ConflictMarkers {

// Whole lines of the text, line endings included: bytes [start, end[.
struct Section {
    int start = 0;
    int end = 0;
    bool operator==(const Section &) const = default;
};

struct Conflict {
    // The whole conflict, from its first marker to the end of its last.
    Section span;
    Section current;
    Section incoming;
    // The markers' lines, from 0.
    int startLine = 0;
    std::optional<int> baseLine;
    int separatorLine = 0;
    int endLine = 0;
    // What follows the first and the last marker ("HEAD", a branch, a commit).
    QString currentLabel;
    QString incomingLabel;
    bool operator==(const Conflict &) const = default;
};

// What a conflict is replaced with.
enum class Resolution {
    Current,  // the current side's lines
    Incoming, // the incoming side's
    Both,     // the current side's, then the incoming side's
};

// The conflicts of `text` (UTF-8), in order. A marker out of sequence is
// ordinary text, and a conflict that is never closed is not one; a start
// marker inside a conflict starts it again from there.
QList<Conflict> find(QByteArrayView text);

// Replacing `span` by `text` resolves a conflict.
struct Replacement {
    Section span;
    QByteArray text;
};

// How `conflict`, found in `text`, is resolved by `resolution`.
Replacement resolve(QByteArrayView text, const Conflict &conflict, Resolution resolution);

} // namespace ConflictMarkers
