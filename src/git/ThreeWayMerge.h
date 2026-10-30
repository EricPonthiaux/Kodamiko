// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArrayList>
#include <QByteArrayView>
#include <QList>

// The three-way merge of a text file, as stretches of lines: what neither
// side changed, what one side changed, what both changed the same way, and
// the conflicts where both changed the same lines differently. Changes with
// no common line between them conflict, as they do for git.
namespace ThreeWayMerge {

enum class ChunkKind {
    Unchanged, // the same in the three versions
    Current,   // changed on the current side only
    Incoming,  // changed on the incoming side only
    BothSame,  // changed the same way on both sides
    Conflict,  // changed differently on both sides
};

// A stretch of the file and its lines in each version, line endings
// included. A side that did not change the stretch has the ancestor's lines.
struct Chunk {
    ChunkKind kind = ChunkKind::Unchanged;
    QByteArrayList base;
    QByteArrayList current;
    QByteArrayList incoming;

    bool operator==(const Chunk &) const = default;
};

// The stretches of the merge of `current` and `incoming` from their common
// ancestor `base` (empty for a file added on both sides), in order: taken
// together, each version's lines give back that version. The lines both
// sides added at the edges of a conflict are left out of it, as BothSame.
QList<Chunk> merge(QByteArrayView base, QByteArrayView current, QByteArrayView incoming);

} // namespace ThreeWayMerge
