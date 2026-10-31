// SPDX-License-Identifier: GPL-3.0-only

#include "SnippetSession.h"

#include "editor/EditorWidget.h"

#include <QTimer>

#include <algorithm>
#include <climits>

namespace {

// After FindBar's (9): the other stops, and the selected one.
constexpr int stopIndicator = 10;
constexpr int currentIndicator = 11;

// Where a position goes when [position, position + length) is deleted.
int positionAfterDeletion(int at, int position, int length)
{
    if (at <= position)
        return at;
    if (at >= position + length)
        return at - length;
    return position;
}

} // namespace

SnippetSession::SnippetSession(EditorWidget *editor)
    : QObject(editor)
    , editor_(editor)
{
    connect(editor_, &ScintillaEditBase::modified, this,
            [this](Scintilla::ModificationFlags type, Scintilla::Position position, Scintilla::Position length) {
                onModified(int(position), int(type), int(length));
            });
    connect(editor_, &ScintillaView::cursorPositionChanged, this, &SnippetSession::onCursorMoved);
}

void SnippetSession::start(const ExpandedSnippet &snippet, int start)
{
    stop();
    const bool numbered = std::any_of(snippet.stops.cbegin(), snippet.stops.cend(),
                                      [](const ExpandedSnippet::Stop &stop) { return stop.index > 0; });
    if (!numbered)
        return;
    const auto bytes = [&snippet, start](int characters) {
        return start + int(snippet.text.left(characters).toUtf8().size());
    };
    QList<Stop> stops;
    for (const ExpandedSnippet::Stop &stop : snippet.stops) {
        Stop converted{stop.index, {}};
        for (const ExpandedSnippet::Range &range : stop.ranges)
            converted.ranges.append({bytes(range.start), bytes(range.start + range.length)});
        stops.append(converted);
    }
    // Without $0, the session ends at the snippet's end.
    if (stops.last().index != 0) {
        const int end = bytes(int(snippet.text.size()));
        stops.append({0, {{end, end}}});
    }
    stops_ = stops;
    current_ = 0;
    const QColor accent = editor_->theme().accent;
    editor_->send(SCI_INDICSETSTYLE, stopIndicator, INDIC_STRAIGHTBOX);
    editor_->send(SCI_INDICSETFORE, stopIndicator, scintillaColor(editor_->theme().mutedText));
    editor_->send(SCI_INDICSETALPHA, stopIndicator, 20);
    editor_->send(SCI_INDICSETOUTLINEALPHA, stopIndicator, 110);
    editor_->send(SCI_INDICSETSTYLE, currentIndicator, INDIC_STRAIGHTBOX);
    editor_->send(SCI_INDICSETFORE, currentIndicator, scintillaColor(accent));
    editor_->send(SCI_INDICSETALPHA, currentIndicator, 40);
    editor_->send(SCI_INDICSETOUTLINEALPHA, currentIndicator, 220);
    for (const int indicator : {stopIndicator, currentIndicator})
        editor_->send(SCI_INDICSETUNDER, indicator, true);
    select();
}

int SnippetSession::currentIndex() const
{
    return isActive() ? stops_[current_].index : -1;
}

void SnippetSession::next()
{
    if (!isActive())
        return;
    ++current_;
    if (current_ >= stops_.size() - 1) {
        // $0: the cursor goes there and the snippet is done.
        const int end = stops_.last().ranges.first().start;
        stop();
        editor_->send(SCI_GOTOPOS, static_cast<unsigned long>(end));
        return;
    }
    select();
}

void SnippetSession::previous()
{
    if (!isActive() || current_ == 0)
        return;
    --current_;
    select();
}

void SnippetSession::stop()
{
    if (!isActive())
        return;
    stops_.clear();
    clearBoxes();
}

void SnippetSession::select()
{
    const Range range = stops_[current_].ranges.first();
    // The cursor at the end, the placeholder selected: typing replaces it.
    editor_->send(SCI_SETSEL, static_cast<unsigned long>(range.start), static_cast<long>(range.end));
    paint();
}

void SnippetSession::clearBoxes()
{
    const long length = editor_->send(SCI_GETLENGTH);
    for (const int indicator : {stopIndicator, currentIndicator}) {
        editor_->send(SCI_SETINDICATORCURRENT, indicator);
        editor_->send(SCI_INDICATORCLEARRANGE, 0L, length);
    }
}

void SnippetSession::paint()
{
    clearBoxes();
    for (int i = 0; i < stops_.size() - 1; ++i) {
        editor_->send(SCI_SETINDICATORCURRENT, i == current_ ? currentIndicator : stopIndicator);
        for (const Range &range : stops_[i].ranges) {
            if (range.end > range.start)
                editor_->send(SCI_INDICATORFILLRANGE, static_cast<unsigned long>(range.start),
                              static_cast<long>(range.end - range.start));
        }
    }
}

void SnippetSession::onModified(int position, int modificationType, int length)
{
    if (!isActive())
        return;
    if (modificationType & (SC_PERFORMED_UNDO | SC_PERFORMED_REDO)) {
        // Positions no longer mean anything; not changed from here.
        QTimer::singleShot(0, this, &SnippetSession::stop);
        return;
    }
    const bool inserted = modificationType & SC_MOD_INSERTTEXT;
    const bool deleted = modificationType & SC_MOD_DELETETEXT;
    if (!inserted && !deleted)
        return;
    if (deleted)
        moveForDeletion(position, length);
    else
        moveForInsertion(position, length);
    if (!synchronizing_ && !synchronizePending_) {
        synchronizePending_ = true;
        // The document cannot change during its own notification.
        QTimer::singleShot(0, this, &SnippetSession::synchronize);
    }
}

void SnippetSession::moveForDeletion(int position, int length)
{
    for (Stop &stop : stops_) {
        for (Range &range : stop.ranges)
            range = {positionAfterDeletion(range.start, position, length),
                     positionAfterDeletion(range.end, position, length)};
    }
}

void SnippetSession::moveForInsertion(int position, int length)
{
    for (int s = 0; s < stops_.size(); ++s) {
        const bool isCurrent = s == current_;
        for (Range &range : stops_[s].ranges) {
            // Text at the edge of another stop is not its own.
            const bool isAtStartOfOther = position == range.start && !isCurrent;
            const bool isAtEndOfCurrent = position == range.end && isCurrent;
            const bool isBefore = position < range.start || isAtStartOfOther;
            const bool isInside = position < range.end || isAtEndOfCurrent;
            if (isBefore) {
                range.start += length;
                range.end += length;
            } else if (isInside) {
                range.end += length; // typed in it
            }
        }
    }
}

void SnippetSession::synchronize()
{
    synchronizePending_ = false;
    if (!isActive())
        return;
    QList<Range> &ranges = stops_[current_].ranges;
    const int caret = int(editor_->send(SCI_GETCURRENTPOS));
    // The occurrence being typed in is the model.
    qsizetype source = 0;
    for (qsizetype i = 0; i < ranges.size(); ++i) {
        if (ranges[i].start <= caret && caret <= ranges[i].end) {
            source = i;
            break;
        }
    }
    const QByteArray text = editor_->utf8Range(ranges[source].start, ranges[source].end);
    synchronizing_ = true;
    for (qsizetype i = 0; i < ranges.size(); ++i) {
        if (i == source || editor_->utf8Range(ranges[i].start, ranges[i].end) == text)
            continue;
        // onModified moves every range, this one included.
        editor_->send(SCI_SETTARGETRANGE, static_cast<unsigned long>(ranges[i].start),
                      static_cast<long>(ranges[i].end));
        editor_->sends(SCI_REPLACETARGET, uintptr_t(text.size()), text.constData());
    }
    synchronizing_ = false;
    paint();
}

void SnippetSession::onCursorMoved()
{
    if (!isActive() || synchronizing_)
        return;
    int first = INT_MAX;
    int last = 0;
    for (const Stop &stop : stops_) {
        for (const Range &range : stop.ranges) {
            first = std::min(first, range.start);
            last = std::max(last, range.end);
        }
    }
    const int caret = int(editor_->send(SCI_GETCURRENTPOS));
    if (caret < first || caret > last)
        stop();
}
