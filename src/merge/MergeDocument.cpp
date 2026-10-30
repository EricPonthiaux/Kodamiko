// SPDX-License-Identifier: GPL-3.0-only

#include "MergeDocument.h"

#include <QtAssert>

#include <algorithm>

namespace {

using ThreeWayMerge::Chunk;
using ThreeWayMerge::ChunkKind;
using Choice = MergeDocument::Choice;
using Segment = MergeDocument::Segment;

// What the result takes, before the user decides, from a stretch of `kind`.
Choice defaultChoice(ChunkKind kind)
{
    switch (kind) {
    case ChunkKind::Incoming:
        return Choice::Incoming;
    case ChunkKind::Conflict:
        return Choice::Unresolved;
    case ChunkKind::Unchanged:
    case ChunkKind::Current:
    case ChunkKind::BothSame:
        return Choice::Current;
    }
    return Choice::Current;
}

// `first` then `second`, on separate lines even when `first` ends the file
// without a line ending: it gets the one its other lines have.
QByteArrayList concatenated(QByteArrayList first, const QByteArrayList &second)
{
    if (!first.isEmpty() && !second.isEmpty() && !first.last().endsWith('\n')) {
        const bool usesCrLf = first.size() > 1 && first[first.size() - 2].endsWith("\r\n");
        first.last() += usesCrLf ? "\r\n" : "\n";
    }
    return first + second;
}

// The kind of the stretch that joins `segments`: a conflict if one of them
// is, or if both sides changed lines in it; otherwise the one kind of
// change they have.
ChunkKind joinedKind(const QList<Segment> &segments)
{
    QList<ChunkKind> changed;
    for (const Segment &segment : segments) {
        if (segment.chunk.kind != ChunkKind::Unchanged && !changed.contains(segment.chunk.kind))
            changed.append(segment.chunk.kind);
    }
    if (changed.isEmpty())
        return ChunkKind::Unchanged;
    if (changed.size() == 1)
        return changed.first();
    return ChunkKind::Conflict;
}

Chunk joinedChunk(const QList<Segment> &segments)
{
    Chunk chunk;
    chunk.kind = joinedKind(segments);
    for (const Segment &segment : segments) {
        chunk.base += segment.chunk.base;
        chunk.current += segment.chunk.current;
        chunk.incoming += segment.chunk.incoming;
    }
    return chunk;
}

QByteArrayList linesOf(const Segment &segment)
{
    const Chunk &chunk = segment.chunk;
    switch (segment.choice) {
    case Choice::Unresolved:
        return chunk.base;
    case Choice::Current:
        return chunk.current;
    case Choice::Incoming:
        return chunk.incoming;
    case Choice::CurrentThenIncoming:
        return concatenated(chunk.current, chunk.incoming);
    case Choice::IncomingThenCurrent:
        return concatenated(chunk.incoming, chunk.current);
    case Choice::Edited:
        return segment.edited;
    }
    return {};
}

} // namespace

MergeDocument::MergeDocument(const QList<ThreeWayMerge::Chunk> &chunks)
{
    for (const Chunk &chunk : chunks)
        segments_.append({chunk, defaultChoice(chunk.kind), {}});
}

QByteArrayList MergeDocument::resultLines(int segment) const
{
    return linesOf(segments_.at(segment));
}

MergeDocument::LineRange MergeDocument::resultRange(int segment) const
{
    return resultRanges().at(segment);
}

QByteArray MergeDocument::resultText() const
{
    QByteArray text;
    for (const Segment &segment : segments_)
        text += linesOf(segment).join();
    return text;
}

bool MergeDocument::isResolved() const
{
    return unresolvedCount() == 0;
}

int MergeDocument::unresolvedCount() const
{
    return int(std::ranges::count(segments_, Choice::Unresolved, &Segment::choice));
}

void MergeDocument::choose(int segment, Choice choice)
{
    Q_ASSERT(choice != Choice::Edited);
    if (segments_.at(segment).choice == choice)
        return;
    remember();
    segments_[segment].choice = choice;
    segments_[segment].edited.clear();
}

void MergeDocument::resolveRemaining(Choice choice)
{
    Q_ASSERT(choice != Choice::Edited && choice != Choice::Unresolved);
    if (isResolved())
        return;
    remember();
    for (Segment &segment : segments_) {
        if (segment.choice == Choice::Unresolved)
            segment.choice = choice;
    }
}

void MergeDocument::replaceResultLines(int first, int count, const QByteArrayList &lines)
{
    remember();
    if (segments_.isEmpty()) {
        segments_.append({{}, Choice::Edited, lines});
        return;
    }
    const auto [firstSegment, lastSegment] = touchedSegments(first, count);
    const QList<LineRange> ranges = resultRanges();
    const QByteArrayList before = resultLines(firstSegment).first(first - ranges[firstSegment].first);
    const QByteArrayList lastLines = resultLines(lastSegment);
    const int keptAfter = ranges[lastSegment].first + ranges[lastSegment].count - (first + count);
    const QByteArrayList after = lastLines.last(std::max(keptAfter, 0));

    const QList<Segment> joined = segments_.mid(firstSegment, lastSegment - firstSegment + 1);
    segments_.remove(firstSegment, joined.size());
    segments_.insert(firstSegment, {joinedChunk(joined), Choice::Edited, before + lines + after});
}

void MergeDocument::undo()
{
    if (history_.isEmpty())
        return;
    undone_.append(segments_);
    segments_ = history_.takeLast();
}

void MergeDocument::redo()
{
    if (undone_.isEmpty())
        return;
    history_.append(segments_);
    segments_ = undone_.takeLast();
}

void MergeDocument::remember()
{
    history_.append(segments_);
    undone_.clear();
}

QList<MergeDocument::LineRange> MergeDocument::resultRanges() const
{
    QList<LineRange> ranges;
    int line = 0;
    for (const Segment &segment : segments_) {
        const int count = int(linesOf(segment).size());
        ranges.append({line, count});
        line += count;
    }
    return ranges;
}

std::pair<int, int> MergeDocument::touchedSegments(int first, int count) const
{
    const QList<LineRange> ranges = resultRanges();
    const auto isOpenAndEmptyAt = [this, &ranges, first](qsizetype i) {
        return ranges[i].count == 0 && ranges[i].first == first && segments_[i].choice == Choice::Unresolved;
    };
    const auto contains = [&ranges](qsizetype i, int line) {
        return ranges[i].first <= line && line < ranges[i].first + ranges[i].count;
    };
    int firstSegment = int(segments_.size()) - 1;
    for (qsizetype i = 0; i < ranges.size(); ++i) {
        if (isOpenAndEmptyAt(i) || contains(i, first)) {
            firstSegment = int(i);
            break;
        }
    }
    if (count == 0)
        return {firstSegment, firstSegment};
    int lastSegment = firstSegment;
    for (qsizetype i = firstSegment + 1; i < ranges.size(); ++i) {
        if (ranges[i].first >= first + count)
            break;
        lastSegment = int(i);
    }
    return {firstSegment, lastSegment};
}
