#pragma once

#include "DiffLayout.h"

#include <QList>

// The two columns of a side-by-side diff, row for row: the old version at
// the left, the new one at the right.
struct DiffColumns {
    QList<DiffLine> oldSide;
    QList<DiffLine> newSide;
};

namespace SideBySide {

// Splits the lines of a unified diff (DiffLayout::lines) into two aligned
// columns. Title bars, separators and unchanged lines are on both sides; a
// removed line faces the added line of the same rank in its block of
// changes, and a Filler line faces a line that has no counterpart. A filler
// leads to the line it faces, for staging. The separators' arrows stay on
// the new side only.
DiffColumns columns(const QList<DiffLine> &lines);

} // namespace SideBySide
