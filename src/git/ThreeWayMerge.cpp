// SPDX-License-Identifier: GPL-3.0-only

#include "ThreeWayMerge.h"

#include "LibGit2.h"

#include <algorithm>

namespace ThreeWayMerge {
namespace {

using Lines = QByteArrayList;

// The lines `[baseStart, baseEnd[` of the ancestor replaced by the lines
// `[sideStart, sideEnd[` of a side, from 0.
struct Hunk {
    int baseStart = 0;
    int baseEnd = 0;
    int sideStart = 0;
    int sideEnd = 0;

    int delta() const { return (sideEnd - sideStart) - (baseEnd - baseStart); }
};

// The hunks of each side that touch one another, and the ancestor's lines
// they cover.
struct Cluster {
    int baseStart = 0;
    int baseEnd = 0;
    QList<Hunk> current;
    QList<Hunk> incoming;
};

Lines splitLines(QByteArrayView text)
{
    Lines lines;
    qsizetype start = 0;
    while (start < text.size()) {
        const qsizetype newline = text.indexOf('\n', start);
        const qsizetype end = newline < 0 ? text.size() : newline + 1;
        lines.append(text.sliced(start, end - start).toByteArray());
        start = end;
    }
    return lines;
}

int hunkCallback(const git_diff_delta *, const git_diff_hunk *hunk, void *payload)
{
    // A side that inserts or deletes only counts its start from the line
    // before: from 0, that is the start itself.
    const int baseStart = hunk->old_lines == 0 ? hunk->old_start : hunk->old_start - 1;
    const int sideStart = hunk->new_lines == 0 ? hunk->new_start : hunk->new_start - 1;
    static_cast<QList<Hunk> *>(payload)->append(
        {baseStart, baseStart + hunk->old_lines, sideStart, sideStart + hunk->new_lines});
    return 0;
}

QList<Hunk> hunksOf(const Lines &base, const Lines &side)
{
    git_diff_options options;
    git_diff_options_init(&options, GIT_DIFF_OPTIONS_VERSION);
    options.context_lines = 0;
    options.interhunk_lines = 0;
    options.flags = GIT_DIFF_FORCE_TEXT;
    const QByteArray baseText = base.join();
    const QByteArray sideText = side.join();
    QList<Hunk> hunks;
    libgit2::check(git_diff_buffers(baseText.constData(), size_t(baseText.size()), nullptr, sideText.constData(),
                                    size_t(sideText.size()), nullptr, &options, nullptr, nullptr, hunkCallback, nullptr,
                                    &hunks),
                   "Comparing the versions of a file");
    return hunks;
}

// Moves into `cluster` the hunks from `hunks[next]` on that start at or
// before its end: they touch it. Returns whether it took one.
bool takeTouching(Cluster &cluster, const QList<Hunk> &hunks, qsizetype &next, QList<Hunk> Cluster::*taken)
{
    bool took = false;
    while (next < hunks.size() && hunks[next].baseStart <= cluster.baseEnd) {
        (cluster.*taken).append(hunks[next]);
        cluster.baseEnd = std::max(cluster.baseEnd, hunks[next].baseEnd);
        ++next;
        took = true;
    }
    return took;
}

int deltaOf(const QList<Hunk> &hunks)
{
    int delta = 0;
    for (const Hunk &hunk : hunks)
        delta += hunk.delta();
    return delta;
}

// Builds the chunks, cluster after cluster, keeping where each version is.
class Builder {
public:
    Builder(Lines base, Lines current, Lines incoming)
        : base_(std::move(base))
        , current_(std::move(current))
        , incoming_(std::move(incoming))
    {
    }

    QList<Chunk> build()
    {
        const QList<Hunk> currentHunks = hunksOf(base_, current_);
        const QList<Hunk> incomingHunks = hunksOf(base_, incoming_);
        qsizetype nextCurrent = 0;
        qsizetype nextIncoming = 0;
        while (nextCurrent < currentHunks.size() || nextIncoming < incomingHunks.size()) {
            Cluster cluster;
            cluster.baseStart = firstStart(currentHunks, nextCurrent, incomingHunks, nextIncoming);
            cluster.baseEnd = cluster.baseStart;
            while (takeTouching(cluster, currentHunks, nextCurrent, &Cluster::current) ||
                   takeTouching(cluster, incomingHunks, nextIncoming, &Cluster::incoming)) { }
            addCluster(cluster);
        }
        addUnchanged(int(base_.size()));
        return chunks_;
    }

private:
    static int firstStart(const QList<Hunk> &current,
                          qsizetype nextCurrent,
                          const QList<Hunk> &incoming,
                          qsizetype nextIncoming)
    {
        if (nextCurrent == current.size())
            return incoming[nextIncoming].baseStart;
        if (nextIncoming == incoming.size())
            return current[nextCurrent].baseStart;
        return std::min(current[nextCurrent].baseStart, incoming[nextIncoming].baseStart);
    }

    void addUnchanged(int baseEnd)
    {
        if (baseEnd > basePosition_) {
            const Lines lines = base_.mid(basePosition_, baseEnd - basePosition_);
            chunks_.append({ChunkKind::Unchanged, lines, lines, lines});
        }
        basePosition_ = baseEnd;
    }

    void addCluster(const Cluster &cluster)
    {
        addUnchanged(cluster.baseStart);
        const int length = cluster.baseEnd - cluster.baseStart;
        const Lines base = base_.mid(cluster.baseStart, length);
        const int currentStart = cluster.baseStart + currentOffset_;
        const int incomingStart = cluster.baseStart + incomingOffset_;
        currentOffset_ += deltaOf(cluster.current);
        incomingOffset_ += deltaOf(cluster.incoming);
        const Lines current = current_.mid(currentStart, cluster.baseEnd + currentOffset_ - currentStart);
        const Lines incoming = incoming_.mid(incomingStart, cluster.baseEnd + incomingOffset_ - incomingStart);
        basePosition_ = cluster.baseEnd;
        if (cluster.incoming.isEmpty())
            chunks_.append({ChunkKind::Current, base, current, incoming});
        else if (cluster.current.isEmpty())
            chunks_.append({ChunkKind::Incoming, base, current, incoming});
        else if (current == incoming)
            chunks_.append({ChunkKind::BothSame, base, current, incoming});
        else
            addConflict(base, current, incoming);
    }

    // The lines both sides added at the start and at the end go out of the
    // conflict, as git's merge does.
    void addConflict(const Lines &base, const Lines &current, const Lines &incoming)
    {
        const qsizetype shorter = std::min(current.size(), incoming.size());
        qsizetype prefix = 0;
        while (prefix < shorter && current[prefix] == incoming[prefix])
            ++prefix;
        qsizetype suffix = 0;
        while (suffix < shorter - prefix &&
               current[current.size() - 1 - suffix] == incoming[incoming.size() - 1 - suffix])
            ++suffix;
        if (prefix > 0)
            chunks_.append({ChunkKind::BothSame, {}, current.first(prefix), incoming.first(prefix)});
        chunks_.append({ChunkKind::Conflict, base, current.mid(prefix, current.size() - prefix - suffix),
                        incoming.mid(prefix, incoming.size() - prefix - suffix)});
        if (suffix > 0)
            chunks_.append({ChunkKind::BothSame, {}, current.last(suffix), incoming.last(suffix)});
    }

    Lines base_;
    Lines current_;
    Lines incoming_;
    QList<Chunk> chunks_;
    // Where the ancestor is, and how far each side's lines are from it.
    int basePosition_ = 0;
    int currentOffset_ = 0;
    int incomingOffset_ = 0;
};

} // namespace

QList<Chunk> merge(QByteArrayView base, QByteArrayView current, QByteArrayView incoming)
{
    const libgit2::Session session;
    return Builder(splitLines(base), splitLines(current), splitLines(incoming)).build();
}

} // namespace ThreeWayMerge
