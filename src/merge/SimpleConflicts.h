// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/ThreeWayMerge.h"

#include <QByteArrayList>

#include <optional>

// The conflicts that need no one to decide: where the two sides differ only
// by whitespace, or changed different words of the lines. Within a
// conflict, the words are merged as git merges lines (ThreeWayMerge): a
// word, a run of spaces, a line ending or another character at a time, so
// that changes with an unchanged word between them combine, and changes to
// the same word, or to touching ones, still conflict.
namespace SimpleConflicts {

// The lines that resolve `conflict` (a ChunkKind::Conflict), or nothing if
// the sides changed the same words: the current side's when the sides
// differ only by whitespace, both sides' changes otherwise.
std::optional<QByteArrayList> resolve(const ThreeWayMerge::Chunk &conflict);

} // namespace SimpleConflicts
