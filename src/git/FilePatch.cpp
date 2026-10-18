#include "FilePatch.h"

#include <QRegularExpression>

#include <algorithm>
#include <optional>

namespace {

// The "@@" line of `hunk`.
QByteArray hunkHeader(const DiffHunk &hunk)
{
    return "@@ -" + QByteArray::number(hunk.oldStart) + ',' + QByteArray::number(hunk.oldCount) + " +" +
           QByteArray::number(hunk.newStart) + ',' + QByteArray::number(hunk.newCount) + " @@" + hunk.heading;
}

// First line number of one side of a hunk: git writes the preceding line
// when that side is empty ("-0,0" for a created file).
int firstLine(int start, int count)
{
    return count > 0 ? start : start + 1;
}

int startFromFirstLine(int first, int count)
{
    return count > 0 ? first : first - 1;
}

// "a/path" ↔ "b/path", quotes included if git added them.
QByteArray otherSide(QByteArray name, char from, char to)
{
    const int letter = name.startsWith('"') ? 1 : 0;
    if (name.size() > letter && name[letter] == from)
        name[letter] = to;
    return name;
}

bool isFileModeOrIndexLine(const QByteArray &line)
{
    return line.startsWith("deleted file mode") || line.startsWith("new file mode") || line.startsWith("index ");
}

// Partial patch of a deleted file (forward) or created file (reverse): the
// file must stay, so the patch becomes a plain modification.
QList<QByteArray> keepFileHeader(const QList<QByteArray> &header)
{
    QByteArray oldName;
    QByteArray newName;
    for (const QByteArray &line : header) {
        if (line.startsWith("--- "))
            oldName = line.mid(4);
        else if (line.startsWith("+++ "))
            newName = line.mid(4);
    }
    QList<QByteArray> result;
    for (const QByteArray &line : header) {
        if (isFileModeOrIndexLine(line))
            continue;
        if (line == "--- /dev/null")
            result.append("--- " + otherSide(newName, 'b', 'a'));
        else if (line == "+++ /dev/null")
            result.append("+++ " + otherSide(oldName, 'a', 'b'));
        else
            result.append(line);
    }
    return result;
}

// The hunk whose "@@" line is `line`, without its lines; nothing if `line`
// is not a hunk header.
std::optional<DiffHunk> parseHunkHeader(const QByteArray &line)
{
    static const QRegularExpression hunkPattern(QStringLiteral(R"(^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@)"));
    if (!line.startsWith("@@ "))
        return std::nullopt;
    const QRegularExpressionMatch match = hunkPattern.match(QString::fromLatin1(line));
    if (!match.hasMatch())
        return std::nullopt;
    // A count git omits is 1.
    const auto count = [&match](int group) {
        return match.captured(group).isEmpty() ? 1 : match.captured(group).toInt();
    };
    DiffHunk hunk;
    hunk.oldStart = match.captured(1).toInt();
    hunk.oldCount = count(2);
    hunk.newStart = match.captured(3).toInt();
    hunk.newCount = count(4);
    hunk.heading = line.mid(match.capturedLength(0));
    return hunk;
}

struct SelectionCoverage {
    bool anySelected = false;
    bool complete = true; // every "+" and "-" line is selected
};

SelectionCoverage coverageOf(const QList<DiffHunk> &hunks, const PatchSelection &selection)
{
    SelectionCoverage coverage;
    for (int i = 0; i < hunks.size(); ++i) {
        const QSet<int> chosen = selection.value(i);
        for (int j = 0; j < hunks[i].lines.size(); ++j) {
            if (!hunks[i].isChange(j))
                continue;
            if (chosen.contains(j))
                coverage.anySelected = true;
            else
                coverage.complete = false;
        }
    }
    return coverage;
}

bool hasHeaderLine(const QList<QByteArray> &header, const char *start)
{
    return std::any_of(header.begin(), header.end(),
                       [start](const QByteArray &line) { return line.startsWith(start); });
}

bool hasChosenChange(const DiffHunk &hunk, const QSet<int> &chosen)
{
    return std::any_of(chosen.begin(), chosen.end(),
                       [&hunk](int line) { return line < hunk.lines.size() && hunk.isChange(line); });
}

// The first character of a diff line: ' ', '+', '-' or '\\'.
char kindOf(const QByteArray &line)
{
    return line.isEmpty() ? ' ' : line[0];
}

// `hunk` keeping only the chosen changes, with both sides counted again.
// The starts are copied as they are.
DiffHunk partialHunk(const DiffHunk &hunk, const QSet<int> &chosen, PatchDirection direction)
{
    // An unselected change stays as on the side the patch applies to.
    const char keptKind = direction == PatchDirection::Forward ? '-' : '+';
    DiffHunk result;
    result.oldStart = hunk.oldStart;
    result.newStart = hunk.newStart;
    result.heading = hunk.heading;
    bool previousKept = false;
    for (int j = 0; j < hunk.lines.size(); ++j) {
        QByteArray line = hunk.lines[j];
        const char kind = kindOf(line);
        if (kind == '\\') {
            if (previousKept)
                result.lines.append(line);
            continue;
        }
        const bool isUnselectedChange = hunk.isChange(j) && !chosen.contains(j);
        previousKept = !isUnselectedChange || kind == keptKind;
        if (!previousKept)
            continue;
        if (isUnselectedChange)
            line[0] = ' ';
        result.lines.append(line);
        result.oldCount += int(kindOf(line) != '+');
        result.newCount += int(kindOf(line) != '-');
    }
    return result;
}

} // namespace

bool DiffHunk::isChange(int line) const
{
    const QByteArray &text = lines.at(line);
    return text.startsWith('+') || text.startsWith('-');
}

FilePatch FilePatch::parse(const QByteArray &patch)
{
    FilePatch result;
    QList<QByteArray> lines = patch.split('\n');
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    for (const QByteArray &line : std::as_const(lines)) {
        if (std::optional<DiffHunk> hunk = parseHunkHeader(line)) {
            result.hunks.append(std::move(*hunk));
            continue;
        }
        if (result.hunks.isEmpty())
            result.header.append(line);
        else
            result.hunks.last().lines.append(line);
    }
    return result;
}

int FilePatch::lineCount() const
{
    int count = static_cast<int>(header.size());
    for (const DiffHunk &hunk : hunks)
        count += 1 + static_cast<int>(hunk.lines.size());
    return count;
}

int FilePatch::addedLines() const
{
    int count = 0;
    for (const DiffHunk &hunk : hunks)
        count += static_cast<int>(std::count_if(hunk.lines.begin(), hunk.lines.end(),
                                                [](const QByteArray &line) { return line.startsWith('+'); }));
    return count;
}

int FilePatch::removedLines() const
{
    int count = 0;
    for (const DiffHunk &hunk : hunks)
        count += static_cast<int>(std::count_if(hunk.lines.begin(), hunk.lines.end(),
                                                [](const QByteArray &line) { return line.startsWith('-'); }));
    return count;
}

std::pair<int, int> FilePatch::locate(int line) const
{
    line -= static_cast<int>(header.size());
    if (line < 0)
        return {-1, -1};
    for (int i = 0; i < hunks.size(); ++i) {
        const int size = 1 + static_cast<int>(hunks[i].lines.size());
        if (line < size)
            return {i, line - 1};
        line -= size;
    }
    return {-1, -1};
}

PatchSelection FilePatch::selectHunk(int hunk) const
{
    PatchSelection selection;
    if (hunk < 0 || hunk >= hunks.size())
        return selection;
    QSet<int> &lines = selection[hunk];
    for (int i = 0; i < hunks[hunk].lines.size(); ++i)
        lines.insert(i);
    return selection;
}

QByteArray FilePatch::partial(const PatchSelection &selection, PatchDirection direction) const
{
    const SelectionCoverage coverage = coverageOf(hunks, selection);
    if (!coverage.anySelected)
        return {};

    // The side the patch applies to (the old one forward, the new one in
    // reverse) is kept as is; only the other side changes.
    const bool forward = direction == PatchDirection::Forward;
    const char *const removedFileMode = forward ? "deleted file mode" : "new file mode";
    const bool fileMustStay = !coverage.complete && hasHeaderLine(header, removedFileMode);
    QList<QByteArray> out = fileMustStay ? keepFileHeader(header) : header;

    int delta = 0; // accumulated offset on the modified side
    for (int i = 0; i < hunks.size(); ++i) {
        const QSet<int> chosen = selection.value(i);
        if (!hasChosenChange(hunks[i], chosen))
            continue;
        DiffHunk part = partialHunk(hunks[i], chosen, direction);
        // Only the start of the modified side is recomputed: the previous
        // hunks may not be fully applied to it.
        if (forward) {
            part.newStart = startFromFirstLine(firstLine(part.oldStart, part.oldCount) + delta, part.newCount);
            delta += part.newCount - part.oldCount;
        } else {
            part.oldStart = startFromFirstLine(firstLine(part.newStart, part.newCount) + delta, part.oldCount);
            delta += part.oldCount - part.newCount;
        }
        out.append(hunkHeader(part));
        out.append(part.lines);
    }
    return out.join('\n') + '\n';
}
