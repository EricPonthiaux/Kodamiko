#include "SideBySide.h"

#include <algorithm>

namespace {

DiffLine fillerFacing(const DiffLine &line)
{
    DiffLine filler;
    filler.kind = DiffLineKind::Filler;
    filler.location = line.location;
    return filler;
}

// The lines of `kind` from `from`, with the notes that follow them ("No
// line break at the end of the file"); returns where the run ends.
qsizetype takeRun(const QList<DiffLine> &lines, qsizetype from, DiffLineKind kind, QList<DiffLine> &run)
{
    qsizetype i = from;
    for (; i < lines.size(); ++i) {
        const bool isNoteOfTheRun = i > from && lines[i].kind == DiffLineKind::Note;
        if (lines[i].kind != kind && !isNoteOfTheRun)
            break;
        run.append(lines[i]);
    }
    return i;
}

// A block of changes: removed lines at the left, added ones at the right,
// the shorter side padded with fillers. Returns where the block ends.
qsizetype appendChanges(DiffColumns &columns, const QList<DiffLine> &lines, qsizetype from)
{
    QList<DiffLine> removed;
    QList<DiffLine> added;
    const qsizetype addedFrom = takeRun(lines, from, DiffLineKind::Removed, removed);
    const qsizetype end = takeRun(lines, addedFrom, DiffLineKind::Added, added);
    const qsizetype rows = std::max(removed.size(), added.size());
    for (qsizetype row = 0; row < rows; ++row) {
        const bool hasRemoved = row < removed.size();
        const bool hasAdded = row < added.size();
        columns.oldSide.append(hasRemoved ? removed[row] : fillerFacing(added[row]));
        columns.newSide.append(hasAdded ? added[row] : fillerFacing(removed[row]));
    }
    return end;
}

void appendToBoth(DiffColumns &columns, const DiffLine &line)
{
    DiffLine oldLine = line;
    oldLine.arrows.clear(); // revealing lines is done from the new side
    columns.oldSide.append(oldLine);
    columns.newSide.append(line);
}

} // namespace

namespace SideBySide {

DiffColumns columns(const QList<DiffLine> &lines)
{
    DiffColumns columns;
    for (qsizetype i = 0; i < lines.size();) {
        const DiffLineKind kind = lines[i].kind;
        if (kind == DiffLineKind::Removed || kind == DiffLineKind::Added) {
            i = appendChanges(columns, lines, i);
            continue;
        }
        appendToBoth(columns, lines[i]);
        ++i;
    }
    return columns;
}

} // namespace SideBySide
