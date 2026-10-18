#include "FilePatch.h"

#include <QRegularExpression>

#include <algorithm>

namespace {

QByteArray hunkHeader(int oldStart, int oldCount, int newStart, int newCount, const QByteArray &heading)
{
    return "@@ -" + QByteArray::number(oldStart) + ',' + QByteArray::number(oldCount) + " +" +
           QByteArray::number(newStart) + ',' + QByteArray::number(newCount) + " @@" + heading;
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

// Partial patch of a deleted file (forward) or created file (reverse): the
// file must stay, so the patch becomes a plain modification.
QList<QByteArray> keepFileHeader(const QList<QByteArray> &header)
{
    QList<QByteArray> result;
    QByteArray oldName;
    QByteArray newName;
    for (const QByteArray &line : header) {
        if (line.startsWith("--- "))
            oldName = line.mid(4);
        else if (line.startsWith("+++ "))
            newName = line.mid(4);
    }
    // "a/path" ↔ "b/path", quotes included if git added them.
    const auto otherSide = [](QByteArray name, char from, char to) {
        const int letter = name.startsWith('"') ? 1 : 0;
        if (name.size() > letter && name[letter] == from)
            name[letter] = to;
        return name;
    };
    for (const QByteArray &line : header) {
        if (line.startsWith("deleted file mode") || line.startsWith("new file mode") || line.startsWith("index "))
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

} // namespace

bool DiffHunk::isChange(int line) const
{
    const QByteArray &text = lines.at(line);
    return text.startsWith('+') || text.startsWith('-');
}

FilePatch FilePatch::parse(const QByteArray &patch)
{
    static const QRegularExpression hunkPattern(QStringLiteral(R"(^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@)"));

    FilePatch result;
    QList<QByteArray> lines = patch.split('\n');
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    for (const QByteArray &line : std::as_const(lines)) {
        if (line.startsWith("@@ ")) {
            const QRegularExpressionMatch match = hunkPattern.match(QString::fromLatin1(line));
            if (match.hasMatch()) {
                DiffHunk hunk;
                hunk.oldStart = match.captured(1).toInt();
                hunk.oldCount = match.captured(2).isEmpty() ? 1 : match.captured(2).toInt();
                hunk.newStart = match.captured(3).toInt();
                hunk.newCount = match.captured(4).isEmpty() ? 1 : match.captured(4).toInt();
                hunk.heading = line.mid(match.capturedLength(0));
                result.hunks.append(hunk);
                continue;
            }
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
    const bool forward = direction == PatchDirection::Forward;
    bool anySelected = false;
    bool complete = true;
    for (int i = 0; i < hunks.size(); ++i) {
        for (int j = 0; j < hunks[i].lines.size(); ++j) {
            if (!hunks[i].isChange(j))
                continue;
            if (selection.value(i).contains(j))
                anySelected = true;
            else
                complete = false;
        }
    }
    if (!anySelected)
        return {};

    // The side the patch applies to (the old one forward, the new one in
    // reverse) is kept as is; only the other side changes.
    const auto hasLine = [this](const char *start) {
        return std::any_of(header.begin(), header.end(),
                           [start](const QByteArray &line) { return line.startsWith(start); });
    };
    const bool fileMustStay = !complete && (forward ? hasLine("deleted file mode") : hasLine("new file mode"));
    QList<QByteArray> out = fileMustStay ? keepFileHeader(header) : header;

    int delta = 0; // accumulated offset on the modified side
    for (int i = 0; i < hunks.size(); ++i) {
        const DiffHunk &hunk = hunks[i];
        const QSet<int> chosen = selection.value(i);
        const bool hasChange = std::any_of(chosen.begin(), chosen.end(), [&hunk](int line) {
            return line < hunk.lines.size() && hunk.isChange(line);
        });
        if (!hasChange)
            continue;

        QList<QByteArray> body;
        int oldCount = 0;
        int newCount = 0;
        bool previousKept = false;
        for (int j = 0; j < hunk.lines.size(); ++j) {
            QByteArray line = hunk.lines[j];
            const char kind = line.isEmpty() ? ' ' : line[0];
            if (kind == '\\') {
                if (previousKept)
                    body.append(line);
                continue;
            }
            if ((kind == '+' || kind == '-') && !chosen.contains(j)) {
                // Unselected change: the line stays as on the side the
                // patch applies to.
                const char kept = forward ? '-' : '+';
                previousKept = kind == kept;
                if (!previousKept)
                    continue;
                line[0] = ' ';
            } else {
                previousKept = true;
            }
            body.append(line);
            const char marker = line.isEmpty() ? ' ' : line[0];
            oldCount += marker != '+';
            newCount += marker != '-';
        }

        // Only the start of the modified side is recomputed: the previous
        // hunks may not be fully applied to it.
        int oldStart = hunk.oldStart;
        int newStart = hunk.newStart;
        if (forward) {
            newStart = startFromFirstLine(firstLine(oldStart, oldCount) + delta, newCount);
            delta += newCount - oldCount;
        } else {
            oldStart = startFromFirstLine(firstLine(newStart, newCount) + delta, oldCount);
            delta += oldCount - newCount;
        }
        out.append(hunkHeader(oldStart, oldCount, newStart, newCount, hunk.heading));
        out.append(body);
    }
    return out.join('\n') + '\n';
}
