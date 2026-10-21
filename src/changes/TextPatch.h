// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/FilePatch.h"

#include <QByteArray>

// A patch between two texts held in memory (a refactoring's preview), as
// git would show it: hunks of changed lines with the unchanged lines
// around them.
namespace TextPatch {

// The changes from `before` to `after`, with `contextLines` unchanged
// lines around each block; blocks closer than that share a hunk. Lines
// keep no "\r". The header is empty.
FilePatch between(const QByteArray &before, const QByteArray &after, int contextLines = 3);

} // namespace TextPatch
