// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Occurrence.h"

#include <QByteArray>
#include <QList>

#include <cstdint>
#include <functional>

// The occurrences of a file as the index keeps them, a few bytes each:
// grouped by word (by increasing id), each group in text order, its size
// first so that a search for one word skips the others. The numbers are
// varints, the offsets and lines differences with the previous ones.
namespace PostingCodec {

struct Posting {
    std::uint32_t wordId = 0;
    std::uint32_t offset = 0;
    std::uint32_t line = 0;
    OccurrenceRole role = OccurrenceRole::Reference;

    bool operator==(const Posting &) const = default;
};

// `postings` in any order.
QByteArray encode(QList<Posting> postings);
// Each posting in (word id, offset) order, until `visit` returns false.
void decode(const QByteArray &encoded, const std::function<bool(const Posting &)> &visit);
// The postings of `wordId` alone.
QList<Posting> find(const QByteArray &encoded, std::uint32_t wordId);
// The ids of the words, in increasing order.
QList<std::uint32_t> wordIds(const QByteArray &encoded);

} // namespace PostingCodec
