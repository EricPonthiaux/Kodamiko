// SPDX-License-Identifier: GPL-3.0-only

#include "DiffLayout.h"

#include "syntax/LanguageRegistry.h"

#include <QCoreApplication>

#include <algorithm>

namespace {

// Unchanged lines a click reveals at one end of those hidden.
constexpr int revealStep = 20;

struct LineNumbers {
    int oldNumber = 0;
    int newNumber = 0;
};

// The new version of a file, for its unchanged lines; no lines when it is
// unknown.
struct NewVersion {
    int lineCount = 0;
    QList<QByteArray> lines; // split only when some are revealed
};

// The unchanged lines before a block (or after the last one), and the
// blocks around them.
struct Gap {
    LineRange lines;
    int oldOffset = 0; // a line's old number minus its new one
    int hunkAbove = -1;
    int hunkBelow = -1;
};

// A gap as shown: the lines revealed from its top, those still hidden, and
// those revealed from its bottom.
struct GapParts {
    LineRange top;
    LineRange hidden;
    LineRange bottom;
};

QString translate(const char *text)
{
    return QCoreApplication::translate("DiffLayout", text);
}

QString linesLabel(int start, int count)
{
    if (count <= 1)
        return translate("line %1").arg(start);
    return translate("lines %1–%2").arg(start).arg(start + count - 1);
}

DiffLine fileTitle(const DiffFile &file, int fileIndex)
{
    const qsizetype slash = file.path.lastIndexOf(QLatin1Char('/'));
    const QString name = file.path.mid(slash + 1);
    QStringList details;
    if (slash >= 0)
        details.append(file.path.left(slash));
    if (!file.status.isEmpty())
        details.append(file.status);
    DiffLine title;
    title.kind = DiffLineKind::FileHeader;
    title.text = details.isEmpty() ? name : name + QStringLiteral("   ") + details.join(QStringLiteral("  ·  "));
    title.emphasis = static_cast<int>(name.size());
    QStringList stats;
    if (const int added = file.patch.addedLines(); added > 0)
        stats.append(QStringLiteral("+%1").arg(added));
    if (const int removed = file.patch.removedLines(); removed > 0)
        stats.append(QStringLiteral("−%1").arg(removed));
    if (!stats.isEmpty()) {
        title.text += QStringLiteral("   ");
        title.statsStart = static_cast<int>(title.text.size());
        title.text += stats.join(QLatin1Char(' '));
    }
    title.location = {fileIndex, -1, -1};
    return title;
}

QString noTextReason(const DiffFile &file)
{
    if (file.isTooLarge)
        return translate("File too large to show");
    const QList<QByteArray> &header = file.patch.header;
    const bool binary = std::any_of(header.begin(), header.end(), [](const QByteArray &line) {
        return line.startsWith("Binary files") || line.startsWith("GIT binary patch");
    });
    return binary ? translate("Binary file") : translate("No changes to the text");
}

DiffLine noTextNote(const DiffFile &file, int fileIndex)
{
    DiffLine note;
    note.kind = DiffLineKind::Note;
    note.text = noTextReason(file);
    note.location = {fileIndex, -1, -1};
    return note;
}

QChar arrowGlyph(Reveal reveal)
{
    switch (reveal) {
    case Reveal::FirstLines:
        return QChar(0x2193); // ↓
    case Reveal::LastLines:
        return QChar(0x2191); // ↑
    case Reveal::AllLines:
        return QChar(0x2195); // ↕
    }
    return QChar(0x2195);
}

// Up from the block below, down from the block above, or both at once when
// a single step shows them all.
QList<Reveal> arrowsFor(const Gap &gap, LineRange hidden)
{
    if (hidden.count() == 0)
        return {};
    const bool canRevealFirst = gap.hunkAbove >= 0;
    const bool canRevealLast = gap.hunkBelow >= 0;
    const bool canRevealAll = canRevealFirst && canRevealLast && hidden.count() <= revealStep;
    if (canRevealAll)
        return {Reveal::AllLines};
    QList<Reveal> arrows;
    if (canRevealLast)
        arrows.append(Reveal::LastLines);
    if (canRevealFirst)
        arrows.append(Reveal::FirstLines);
    return arrows;
}

// "⋯ ↑ ↓  label", or "⋯ label" without arrows.
DiffLine separatorLine(DiffLineKind kind, const QString &label, const Gap &gap, LineRange hidden)
{
    DiffLine separator;
    separator.kind = kind;
    separator.hidden = hidden;
    separator.text = QStringLiteral("⋯ ");
    const QList<Reveal> arrows = arrowsFor(gap, hidden);
    for (const Reveal reveal : arrows) {
        separator.arrows.append({int(separator.text.size()), reveal});
        separator.text += arrowGlyph(reveal) + QStringLiteral(" ");
    }
    if (!arrows.isEmpty())
        separator.text += QLatin1Char(' ');
    separator.text += label;
    return separator;
}

DiffLine hunkSeparator(const DiffHunk &hunk, const Gap &gap, LineRange hidden, bool withHeading)
{
    QString label =
        hunk.newCount > 0 ? linesLabel(hunk.newStart, hunk.newCount) : linesLabel(hunk.oldStart, hunk.oldCount);
    if (const QString heading = QString::fromUtf8(hunk.heading).trimmed(); withHeading && !heading.isEmpty())
        label += QStringLiteral("  ·  ") + heading;
    return separatorLine(DiffLineKind::HunkHeader, label, gap, hidden);
}

DiffLine patchLine(const QByteArray &raw, DiffLocation location, LineNumbers &numbers)
{
    DiffLine line;
    line.location = location;
    line.text = QString::fromUtf8(raw.mid(1));
    if (line.text.endsWith(QLatin1Char('\r')))
        line.text.chop(1);
    if (raw.startsWith('+')) {
        line.kind = DiffLineKind::Added;
        line.newNumber = numbers.newNumber++;
    } else if (raw.startsWith('-')) {
        line.kind = DiffLineKind::Removed;
        line.oldNumber = numbers.oldNumber++;
    } else if (raw.startsWith('\\')) {
        line.kind = DiffLineKind::Note;
        line.text = translate("No line break at the end of the file");
    } else {
        line.kind = DiffLineKind::Context;
        line.oldNumber = numbers.oldNumber++;
        line.newNumber = numbers.newNumber++;
    }
    return line;
}

qsizetype endOfRun(const QList<DiffLine> &lines, qsizetype from, DiffLineKind kind)
{
    qsizetype end = from;
    while (end < lines.size() && lines[end].kind == kind)
        ++end;
    return end;
}

void markChangedWords(QList<DiffLine> &lines, qsizetype first)
{
    for (qsizetype i = first; i < lines.size();) {
        const qsizetype removedEnd = endOfRun(lines, i, DiffLineKind::Removed);
        const qsizetype addedEnd = endOfRun(lines, removedEnd, DiffLineKind::Added);
        const qsizetype pairs = std::min(removedEnd - i, addedEnd - removedEnd);
        for (qsizetype k = 0; k < pairs; ++k) {
            auto [before, after] = WordDiff::changes(lines[i + k].text, lines[removedEnd + k].text);
            lines[i + k].changes = std::move(before);
            lines[removedEnd + k].changes = std::move(after);
        }
        i = std::max(addedEnd, i + 1);
    }
}

void appendHunkLines(QList<DiffLine> &lines, const DiffHunk &hunk, DiffLocation location)
{
    const qsizetype first = lines.size();
    LineNumbers numbers{hunk.oldStart, hunk.newStart};
    for (int l = 0; l < hunk.lines.size(); ++l)
        lines.append(patchLine(hunk.lines[l], {location.file, location.hunk, l}, numbers));
    markChangedWords(lines, first);
}

// The new-version line a block starts at, and the one after it. A side
// without lines ("+4,0") sits after its start line.
int firstNewLine(const DiffHunk &hunk)
{
    return hunk.newCount > 0 ? hunk.newStart : hunk.newStart + 1;
}

int nextNewLine(const DiffHunk &hunk)
{
    return hunk.newCount > 0 ? hunk.newStart + hunk.newCount : hunk.newStart + 1;
}

int nextOldLine(const DiffHunk &hunk)
{
    return hunk.oldCount > 0 ? hunk.oldStart + hunk.oldCount : hunk.oldStart + 1;
}

NewVersion newVersionOf(const DiffFile &file, const QSet<int> &revealed)
{
    NewVersion version;
    const QByteArray &text = file.newText;
    if (text.isEmpty())
        return version;
    version.lineCount = int(text.count('\n')) + (text.endsWith('\n') ? 0 : 1);
    if (!revealed.isEmpty())
        version.lines = text.split('\n');
    return version;
}

// The gap before block `hunk`; after the last block for `hunk` == their
// count. Empty without the new version: its lines could not be shown.
Gap gapBefore(const FilePatch &patch, int hunk, const NewVersion &version)
{
    Gap gap;
    if (version.lineCount == 0)
        return gap;
    const int hunkCount = int(patch.hunks.size());
    gap.hunkAbove = hunk - 1;
    gap.hunkBelow = hunk < hunkCount ? hunk : -1;
    if (hunk > 0) {
        const DiffHunk &above = patch.hunks[hunk - 1];
        gap.lines.first = nextNewLine(above);
        gap.oldOffset = nextOldLine(above) - gap.lines.first;
    } else {
        gap.lines.first = 1;
    }
    gap.lines.last = hunk < hunkCount ? firstNewLine(patch.hunks[hunk]) - 1 : version.lineCount;
    return gap;
}

GapParts splitGap(LineRange gap, const QSet<int> &revealed)
{
    int top = gap.first;
    while (top <= gap.last && revealed.contains(top))
        ++top;
    int bottom = gap.last;
    while (bottom >= top && revealed.contains(bottom))
        --bottom;
    return {{gap.first, top - 1}, {top, bottom}, {bottom + 1, gap.last}};
}

QList<DiffLine> unchangedLines(const NewVersion &version, LineRange range, int oldOffset, DiffLocation location)
{
    QList<DiffLine> lines;
    for (int number = range.first; number <= range.last; ++number) {
        DiffLine line;
        line.location = location;
        line.text = QString::fromUtf8(version.lines.value(number - 1));
        if (line.text.endsWith(QLatin1Char('\r')))
            line.text.chop(1);
        line.newNumber = number;
        line.oldNumber = number + oldOffset;
        lines.append(line);
    }
    return lines;
}

// "⋯ ↓  17 more lines", after the last block.
DiffLine trailingSeparator(const Gap &gap, LineRange hidden, DiffLocation location)
{
    const QString label = QCoreApplication::translate("DiffLayout", "%n more lines", nullptr, hidden.count());
    DiffLine separator = separatorLine(DiffLineKind::HiddenLines, label, gap, hidden);
    separator.location = location;
    return separator;
}

// The blocks of `file` and the unchanged lines between them: those
// revealed, and a separator where some remain hidden.
void appendFileBody(QList<DiffLine> &lines, const DiffFile &file, int fileIndex, const QSet<int> &revealed)
{
    const FilePatch &patch = file.patch;
    // Git takes the heading of a block from the last line starting with
    // a letter: in prose, a sentence cut short.
    const QString language = LanguageRegistry::languageForFile(file.path);
    const bool headings = language != LanguageRegistry::plainText && language != QLatin1String("markdown");
    const NewVersion version = newVersionOf(file, revealed);
    const int hunkCount = int(patch.hunks.size());
    for (int h = 0; h <= hunkCount; ++h) {
        const Gap gap = gapBefore(patch, h, version);
        const GapParts parts = splitGap(gap.lines, revealed);
        const DiffLocation above{fileIndex, gap.hunkAbove, -1};
        lines += unchangedLines(version, parts.top, gap.oldOffset, above);
        if (h == hunkCount) {
            if (parts.hidden.count() > 0)
                lines.append(trailingSeparator(gap, parts.hidden, above));
            lines += unchangedLines(version, parts.bottom, gap.oldOffset, above);
            return;
        }
        const DiffLocation block{fileIndex, h, -1};
        lines.append(hunkSeparator(patch.hunks[h], gap, parts.hidden, headings));
        lines.last().location = block;
        lines += unchangedLines(version, parts.bottom, gap.oldOffset, block);
        appendHunkLines(lines, patch.hunks[h], block);
    }
}

} // namespace

namespace DiffLayout {

QString revealKey(const DiffFile &file)
{
    return file.status.isEmpty() ? file.path : file.path + QLatin1Char('\n') + file.status;
}

QList<DiffLine> lines(const QList<DiffFile> &files, const RevealedLines &revealed)
{
    QList<DiffLine> lines;
    for (int f = 0; f < files.size(); ++f) {
        const DiffFile &file = files[f];
        // A blank line before each file: they breathe.
        DiffLine spacer;
        spacer.kind = DiffLineKind::Spacer;
        lines.append(spacer);
        lines.append(fileTitle(file, f));

        if (file.patch.hunks.isEmpty())
            lines.append(noTextNote(file, f));
        else
            appendFileBody(lines, file, f, revealed.value(revealKey(file)));
    }
    return lines;
}

LineRange revealedPart(LineRange hidden, Reveal reveal)
{
    switch (reveal) {
    case Reveal::FirstLines:
        return {hidden.first, std::min(hidden.last, hidden.first + revealStep - 1)};
    case Reveal::LastLines:
        return {std::max(hidden.first, hidden.last - revealStep + 1), hidden.last};
    case Reveal::AllLines:
        return hidden;
    }
    return hidden;
}

} // namespace DiffLayout
