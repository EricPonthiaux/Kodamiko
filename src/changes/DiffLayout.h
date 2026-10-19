#pragma once

#include "WordDiff.h"
#include "git/FilePatch.h"

#include <QList>
#include <QString>

// File shown by a DiffView.
struct DiffFile {
    QString path;   // as shown in its title bar
    QString status; // "modified", "added", "renamed from x.cpp"…
    FilePatch patch;
    bool isTooLarge = false; // listed without its patch, see FileDiff
};

enum class DiffLineKind {
    Spacer,
    FileHeader,
    HunkHeader,
    Context,
    Added,
    Removed,
    Note
};

// What a displayed line stands for in the files' patches. `hunk` is -1 on a
// file's title bar, `line` -1 on a block separator.
struct DiffLocation {
    int file = -1;
    int hunk = -1;
    int line = -1;
};

// A line of a diff as displayed, without git's notation.
struct DiffLine {
    QString text;
    DiffLineKind kind = DiffLineKind::Context;
    DiffLocation location;
    int oldNumber = 0; // from 1; 0 when the line is not on that side
    int newNumber = 0;
    int emphasis = 0;    // title bars: characters of the file name, shown brighter
    int statsStart = -1; // title bars: where "+12 −3" begins
    // Added or removed lines facing their counterpart: the words that changed.
    QList<TextRange> changes;
};

namespace DiffLayout {

// The displayed lines for `files`: per file a blank line and a title bar,
// then per block a separator ("⋯ lines 40–48 · function") and its lines.
QList<DiffLine> lines(const QList<DiffFile> &files);

} // namespace DiffLayout
