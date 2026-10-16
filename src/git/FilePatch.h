#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>

// Hunk of a unified diff.
struct DiffHunk {
    int oldStart = 0;
    int oldCount = 0;
    int newStart = 0;
    int newCount = 0;
    QByteArray heading; // text after the second "@@" (enclosing function…)
    // Lines of the hunk, without their "\n": "␠" context, "-" removed,
    // "+" added, "\" "No newline at end of file".
    QList<QByteArray> lines;

    // True for a "+" or "-" line.
    bool isChange(int line) const;
};

// Selected lines of a diff: hunk number → line numbers within the hunk.
using PatchSelection = QHash<int, QSet<int>>;

// Direction in which a partial patch is applied. Forward: applied as is (stage
// lines). Reverse: applied with "git apply -R" (unstage lines, discard lines
// on disk).
enum class PatchDirection { Forward, Reverse };

// Unified diff of a file, as produced by git, split into header and hunks.
// Used to build partial patches for "git apply".
struct FilePatch {
    QList<QByteArray> header; // lines before the first hunk
    QList<DiffHunk> hunks;

    static FilePatch parse(const QByteArray &patch);

    // Number of lines in the diff: header, then for each hunk its "@@" line
    // and its lines.
    int lineCount() const;
    // Position of a diff line: hunk number (-1 in the header) and line
    // number within the hunk (-1 on the "@@" line).
    std::pair<int, int> locate(int line) const;

    // The whole hunk `hunk`.
    PatchSelection selectHunk(int hunk) const;

    // Patch keeping only the selected lines, to be applied in `direction`;
    // the other changes stay as they are. Returns an empty array if no "+"
    // or "-" line is selected.
    QByteArray partial(const PatchSelection &selection, PatchDirection direction) const;
};
