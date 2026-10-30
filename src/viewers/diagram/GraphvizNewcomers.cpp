// SPDX-License-Identifier: GPL-3.0-only

#include "Board.h"
#include "Family.h"
#include "Graphviz.h"

#include <QHash>
#include <QRectF>
#include <QSet>

#include <algorithm>

namespace diagram::graphviz {

namespace {

// --- Newcomers (placeNewcomers)

// The full layout, on a copy: where each shape stands relative to the others.
QHash<QString, QPointF> fullLayoutPositions(const Board &board, const ArrangeOptions &options)
{
    Board scratch;
    scratch.setFamily(board.family());
    scratch.setStyle(board.style());
    scratch.fromJson(board.toJson());
    scratch.meta = board.meta;
    QHash<QString, QPointF> positions;
    if (scratch.family() && scratch.family()->autoArrange)
        scratch.family()->autoArrange(scratch);
    else if (!autoArrange(scratch, options))
        return positions;
    for (const auto &s : scratch.shapes())
        positions.insert(s->id, QPointF(s->x, s->y));
    return positions;
}

QRectF unite(const QRectF &area, const QRectF &rect)
{
    return area.isNull() ? rect : area.united(rect);
}

// The other end of `l` when `id` is one of its ends, or nothing.
QString otherEnd(const Link &l, const QString &id)
{
    if (l.from == id)
        return l.to;
    if (l.to == id)
        return l.from;
    return {};
}

// Puts new shapes one by one next to the placed ones, which never move.
class NewcomerPlacement {
public:
    NewcomerPlacement(Board &board, QHash<QString, QPointF> laidOut, const QSet<QString> &newcomers)
        : board_(board)
        , laidOut_(std::move(laidOut))
    {
        for (const auto &s : board.shapes()) {
            if (newcomers.contains(s->id))
                continue;
            placed_.insert(s->id);
            existing_ = unite(existing_, s->rect());
        }
        looseY_ = existing_.top();
    }

    // Places `s`, unless it has no placed neighbor yet and `canWait`: a
    // later round may give it one. False when it waits.
    bool place(Shape &s, bool canWait)
    {
        const Shape *anchor = placedNeighbor(s.id);
        if (!anchor && canWait)
            return false;
        position(s, anchor);
        // Off any overlap with what is already there.
        for (int attempt = 0; attempt < 60 && overlapsPlaced(s); ++attempt)
            s.y += 24;
        board_.containShape(s, true);
        placed_.insert(s.id);
        existing_ = unite(existing_, s.rect());
        return true;
    }

private:
    const Shape *placedNeighbor(const QString &id) const
    {
        for (const auto &l : board_.links()) {
            const QString other = otherEnd(*l, id);
            if (!other.isEmpty() && other != id && placed_.contains(other))
                return board_.shape(other);
        }
        return nullptr;
    }

    void position(Shape &s, const Shape *anchor)
    {
        if (!anchor) {
            // Related to nothing placed: beside the diagram.
            s.x = (existing_.isNull() ? 0 : existing_.right()) + 80;
            s.y = looseY_;
            looseY_ += s.h + 40;
            return;
        }
        const bool wasLaidOut = laidOut_.contains(s.id) && laidOut_.contains(anchor->id);
        if (!wasLaidOut) {
            s.x = anchor->x;
            s.y = anchor->y + anchor->h + 60;
            return;
        }
        const QPointF offset = laidOut_.value(s.id) - laidOut_.value(anchor->id);
        s.x = anchor->x + offset.x();
        s.y = anchor->y + offset.y();
    }

    bool overlapsPlaced(const Shape &candidate) const
    {
        const QRectF box = candidate.rect().adjusted(-12, -12, 12, 12);
        return std::ranges::any_of(board_.shapes(), [&](const auto &other) {
            return other.get() != &candidate && placed_.contains(other->id) && !other->drawBehindLinks() &&
                   box.intersects(other->rect());
        });
    }

    Board &board_;
    QHash<QString, QPointF> laidOut_;
    QSet<QString> placed_;
    QRectF existing_;
    qreal looseY_ = 0;
};

} // namespace

void placeNewcomers(Board &board, const QStringList &ids, const ArrangeOptions &options)
{
    if (ids.isEmpty())
        return;
    board.measureAll();
    const QSet<QString> newcomers(ids.begin(), ids.end());
    NewcomerPlacement placement(board, fullLayoutPositions(board, options), newcomers);
    // Breadth first from the placed shapes, so that a chain of new shapes
    // follows the neighbor it hangs from.
    QStringList pending = ids;
    for (int round = 0; !pending.isEmpty() && round <= ids.size(); ++round) {
        QStringList next;
        for (const QString &id : pending) {
            Shape *s = board.shape(id);
            if (s && !placement.place(*s, round < ids.size()))
                next.append(id);
        }
        pending = next;
    }
    board.resolveLabels();
    board.touch();
}

} // namespace diagram::graphviz
