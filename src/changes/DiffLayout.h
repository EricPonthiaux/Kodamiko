#pragma once

#include "WordDiff.h"
#include "git/FilePatch.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <algorithm>

// File shown by a DiffView.
struct DiffFile {
    QString path;   // as shown in its title bar
    QString status; // "modified", "added", "renamed from x.cpp"…
    FilePatch patch;
    bool isTooLarge = false; // listed without its patch, see FileDiff
    // The new version, for the unchanged lines around the blocks (see
    // FileDiff::newText); without it, they stay hidden.
    QByteArray newText;
};

enum class DiffLineKind {
    Spacer,
    FileHeader,
    HunkHeader,
    // After the last block: the separator of the unchanged lines up to the
    // end of the file.
    HiddenLines,
    Context,
    Added,
    Removed,
    Note,
    // Side by side: the blank facing a line the other side does not have.
    Filler
};

// What a displayed line stands for in the files' patches. `hunk` is -1 on a
// file's title bar, `line` -1 on a block separator. An unchanged line
// revealed around a block has the block's `hunk` (-1 above the first one)
// and a `line` of -1: it is not in the patch.
struct DiffLocation {
    int file = -1;
    int hunk = -1;
    int line = -1;
};

// Lines of a file's new version, numbered from 1; empty when last < first.
struct LineRange {
    int first = 0;
    int last = -1;

    int count() const { return std::max(0, last - first + 1); }

    bool operator==(const LineRange &) const = default;
};

// What a separator's arrow reveals of the unchanged lines it hides: the
// first ones (↓, below the lines above), the last ones (↑, above the block
// below), or all of them (↕).
enum class Reveal {
    FirstLines,
    LastLines,
    AllLines
};

struct RevealArrow {
    int position = 0; // in characters, in the separator's text
    Reveal reveal = Reveal::AllLines;
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
    // Separators: the unchanged lines hidden at their place, and the arrows
    // that reveal them.
    LineRange hidden;
    QList<RevealArrow> arrows;
};

namespace DiffLayout {

// Unchanged lines to show around the blocks, per file path: their numbers in
// the new version.
using RevealedLines = QHash<QString, QSet<int>>;

// The displayed lines for `files`: per file a blank line and a title bar,
// then per block a separator ("⋯ ↑ ↓  lines 40–48 · function") and its
// lines. The `revealed` lines of a file that has its new version show
// around its blocks; the separator above a block holds those still hidden
// above it, and a last one those after the last block.
QList<DiffLine> lines(const QList<DiffFile> &files, const RevealedLines &revealed = {});

// The lines `reveal` shows of `hidden`: 20 at most at one end, or all.
LineRange revealedPart(LineRange hidden, Reveal reveal);

} // namespace DiffLayout
