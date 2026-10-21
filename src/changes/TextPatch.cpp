// SPDX-License-Identifier: GPL-3.0-only

#include "TextPatch.h"

#include "git/LineChanges.h"

#include <QList>

#include <algorithm>

namespace {

// The changes of `changes` in one hunk, from `first` to `last`.
struct Group {
    qsizetype first = 0;
    qsizetype last = 0;
};

// The lines of the two texts.
struct Texts {
    QList<QByteArray> before;
    QList<QByteArray> after;
};

QList<QByteArray> linesOf(const QByteArray &text)
{
    QList<QByteArray> lines = text.split('\n');
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast(); // after the last line ending
    for (QByteArray &line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);
    }
    return lines;
}

QList<Group> groupsOf(const QList<LineChange> &changes, int contextLines)
{
    QList<Group> groups;
    for (qsizetype i = 0; i < changes.size(); ++i) {
        const bool joinsPrevious =
            !groups.isEmpty() &&
            changes[i].baseStart - (changes[i - 1].baseStart + changes[i - 1].baseCount) <= 2 * contextLines;
        if (joinsPrevious)
            groups.last().last = i;
        else
            groups.append({i, i});
    }
    return groups;
}

// git numbers lines from 1, and an empty side by the line before it.
int unifiedStart(int start, int count)
{
    return count > 0 ? start + 1 : start;
}

DiffHunk hunkOf(const QList<LineChange> &changes, const Group &group, const Texts &texts, int contextLines)
{
    const LineChange &first = changes[group.first];
    const LineChange &last = changes[group.last];
    const int oldFrom = std::max(0, first.baseStart - contextLines);
    const int oldTo = std::min(int(texts.before.size()), last.baseStart + last.baseCount + contextLines);
    const int newFrom = first.start - (first.baseStart - oldFrom);
    DiffHunk hunk;
    int oldLine = oldFrom;
    for (qsizetype i = group.first; i <= group.last; ++i) {
        const LineChange &change = changes[i];
        for (; oldLine < change.baseStart; ++oldLine)
            hunk.lines.append(' ' + texts.before[oldLine]);
        for (int line = change.baseStart; line < change.baseStart + change.baseCount; ++line)
            hunk.lines.append('-' + texts.before[line]);
        for (int line = change.start; line < change.start + change.count; ++line)
            hunk.lines.append('+' + texts.after[line]);
        oldLine = change.baseStart + change.baseCount;
    }
    for (; oldLine < oldTo; ++oldLine)
        hunk.lines.append(' ' + texts.before[oldLine]);
    hunk.oldCount = int(std::ranges::count_if(hunk.lines, [](const QByteArray &line) { return line[0] != '+'; }));
    hunk.newCount = int(std::ranges::count_if(hunk.lines, [](const QByteArray &line) { return line[0] != '-'; }));
    hunk.oldStart = unifiedStart(oldFrom, hunk.oldCount);
    hunk.newStart = unifiedStart(newFrom, hunk.newCount);
    return hunk;
}

} // namespace

namespace TextPatch {

FilePatch between(const QByteArray &before, const QByteArray &after, int contextLines)
{
    const QList<LineChange> changes = LineChanges::compute(before, after);
    const Texts texts{linesOf(before), linesOf(after)};
    FilePatch patch;
    for (const Group &group : groupsOf(changes, contextLines))
        patch.hunks.append(hunkOf(changes, group, texts, contextLines));
    return patch;
}

} // namespace TextPatch
