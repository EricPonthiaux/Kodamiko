// SPDX-License-Identifier: GPL-3.0-only

// DiffView: the notes, in boxes under their lines.

#include "DiffStyles.h"
#include "DiffView.h"

#include <algorithm>

using namespace DiffStyles;

namespace {

// Notes wrap at this width, in characters: the view itself does not wrap.
constexpr int noteColumns = 100;

// `text` cut at spaces into lines of `columns` characters at most, a word
// longer than that alone on its line.
QStringList wrapped(const QString &text, int columns)
{
    QStringList lines;
    for (const QString &paragraph : text.split(QLatin1Char('\n'))) {
        QString line;
        for (const QString &word : paragraph.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            const bool fits = line.isEmpty() || line.size() + 1 + word.size() <= columns;
            if (!fits) {
                lines.append(line);
                line.clear();
            }
            line += line.isEmpty() ? word : QLatin1Char(' ') + word;
        }
        lines.append(line);
    }
    return lines;
}

} // namespace

void DiffView::setNotes(QList<DiffNote> notes)
{
    notes_ = std::move(notes);
    showNotes();
}

QList<int> DiffView::notesAt(int line) const
{
    QList<int> notes;
    for (int i = 0; i < noteLines_.size(); ++i) {
        if (noteLines_[i] == line)
            notes.append(i);
    }
    return notes;
}

void DiffView::showNote(int note)
{
    const int line = noteLines_.value(note, -1);
    if (line < 0)
        return;
    setCursorPosition(line, 0); // scrolls to the cursor: first
    // A third down the view, its box below it in view as well.
    const int row = int(send(SCI_VISIBLEFROMDOCLINE, line));
    setFirstVisibleLine(std::max(0, row - int(send(SCI_LINESONSCREEN)) / 3));
}

void DiffView::goToNote(Direction direction)
{
    QList<int> lines = noteLines_;
    lines.removeAll(-1);
    std::ranges::sort(lines);
    const int start = searchStart();
    std::optional<int> found;
    if (direction == Direction::Next) {
        const auto after = std::ranges::upper_bound(lines, start);
        if (after != lines.end())
            found = *after;
    } else {
        const auto atOrAfter = std::ranges::lower_bound(lines, start);
        if (atOrAfter != lines.begin())
            found = *std::prev(atOrAfter);
    }
    if (found)
        showNote(int(noteLines_.indexOf(*found)));
}

int DiffView::lineOfNote(const DiffNote &note) const
{
    if (note.file < 0 || note.file >= fileStarts_.size())
        return -1;
    for (int i = fileStarts_[note.file]; i < lines_.size() && lines_[i].location.file == note.file; ++i) {
        if (lines_[i].newNumber == note.newLine)
            return i;
    }
    return fileStarts_[note.file];
}

void DiffView::showNotes()
{
    send(SCI_ANNOTATIONCLEARALL);
    if (oldSide_)
        oldSide_->send(SCI_ANNOTATIONCLEARALL);
    noteLines_.clear();
    QHash<int, QStringList> texts;
    QSet<int> errorLines;
    for (const DiffNote &note : std::as_const(notes_)) {
        const int line = lineOfNote(note);
        noteLines_.append(line);
        if (line < 0)
            continue;
        const bool isOnItsLine = lines_[line].newNumber == note.newLine;
        const QString text = isOnItsLine ? note.text : tr("Line %1 · %2").arg(note.newLine).arg(note.text);
        texts[line] += wrapped(text, noteColumns);
        if (note.severity == Diagnostic::Severity::Error)
            errorLines.insert(line);
    }
    const bool isSplit = mode_ == Mode::SideBySide && oldSide_;
    for (auto it = texts.cbegin(); it != texts.cend(); ++it) {
        const int style = errorLines.contains(it.key()) ? noteErrorStyle : noteWarningStyle;
        const QByteArray text = it.value().join(QLatin1Char('\n')).toUtf8();
        sends(SCI_ANNOTATIONSETTEXT, it.key(), text.constData());
        send(SCI_ANNOTATIONSETSTYLE, it.key(), style);
        if (!isSplit)
            continue;
        const QByteArray blank = QStringList(it.value().size(), QStringLiteral(" ")).join(QLatin1Char('\n')).toUtf8();
        oldSide_->sends(SCI_ANNOTATIONSETTEXT, it.key(), blank.constData());
        oldSide_->send(SCI_ANNOTATIONSETSTYLE, it.key(), noteFillerStyle);
    }
}
