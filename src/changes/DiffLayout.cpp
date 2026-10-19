#include "DiffLayout.h"

#include "syntax/LanguageRegistry.h"

#include <QCoreApplication>

#include <algorithm>

namespace {

struct LineNumbers {
    int oldNumber = 0;
    int newNumber = 0;
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

DiffLine hunkSeparator(const DiffHunk &hunk, DiffLocation location, bool withHeading)
{
    DiffLine separator;
    separator.kind = DiffLineKind::HunkHeader;
    const QString lines =
        hunk.newCount > 0 ? linesLabel(hunk.newStart, hunk.newCount) : linesLabel(hunk.oldStart, hunk.oldCount);
    separator.text = QStringLiteral("⋯ ") + lines;
    if (const QString heading = QString::fromUtf8(hunk.heading).trimmed(); withHeading && !heading.isEmpty())
        separator.text += QStringLiteral("  ·  ") + heading;
    separator.location = location;
    return separator;
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

void appendHunk(QList<DiffLine> &lines, const DiffHunk &hunk, DiffLocation location, bool withHeading)
{
    lines.append(hunkSeparator(hunk, location, withHeading));
    const qsizetype first = lines.size();
    LineNumbers numbers{hunk.oldStart, hunk.newStart};
    for (int l = 0; l < hunk.lines.size(); ++l)
        lines.append(patchLine(hunk.lines[l], {location.file, location.hunk, l}, numbers));
    markChangedWords(lines, first);
}

} // namespace

namespace DiffLayout {

QList<DiffLine> lines(const QList<DiffFile> &files)
{
    QList<DiffLine> lines;
    for (int f = 0; f < files.size(); ++f) {
        const DiffFile &file = files[f];
        // A blank line before each file: they breathe.
        DiffLine spacer;
        spacer.kind = DiffLineKind::Spacer;
        lines.append(spacer);
        lines.append(fileTitle(file, f));

        const FilePatch &patch = file.patch;
        if (patch.hunks.isEmpty()) {
            lines.append(noTextNote(file, f));
            continue;
        }
        // Git takes the heading of a block from the last line starting with
        // a letter: in prose, a sentence cut short.
        const QString language = LanguageRegistry::languageForFile(file.path);
        const bool headings = language != LanguageRegistry::plainText && language != QLatin1String("markdown");
        for (int h = 0; h < patch.hunks.size(); ++h)
            appendHunk(lines, patch.hunks[h], {f, h, -1}, headings);
    }
    return lines;
}

} // namespace DiffLayout
