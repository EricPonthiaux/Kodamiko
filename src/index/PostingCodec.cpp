// SPDX-License-Identifier: GPL-3.0-only

#include "PostingCodec.h"

#include "VarintReader.h"

#include <algorithm>

namespace PostingCodec {

namespace {

// A group's header: its word, its number of postings, its size in bytes.
struct GroupHeader {
    std::uint32_t wordId = 0;
    std::uint32_t count = 0;
    std::uint32_t size = 0;
};

GroupHeader readHeader(VarintReader &reader, std::uint32_t previousWordId)
{
    GroupHeader header;
    header.wordId = previousWordId + reader.varint();
    header.count = reader.varint();
    header.size = reader.varint();
    return header;
}

// Calls `visit` for each posting of the group; false if it stopped.
bool readGroup(VarintReader &reader, const GroupHeader &header, const std::function<bool(const Posting &)> &visit)
{
    Posting posting{.wordId = header.wordId};
    for (std::uint32_t i = 0; i < header.count; ++i) {
        posting.offset += reader.varint();
        posting.line += reader.varint();
        posting.role = static_cast<OccurrenceRole>(reader.byte());
        if (!visit(posting))
            return false;
    }
    return true;
}

QByteArray encodeGroup(const QList<Posting> &postings, qsizetype first, qsizetype last)
{
    QByteArray entries;
    std::uint32_t offset = 0;
    std::uint32_t line = 0;
    for (qsizetype i = first; i < last; ++i) {
        appendVarint(entries, postings[i].offset - offset);
        appendVarint(entries, postings[i].line - line);
        entries.append(char(postings[i].role));
        offset = postings[i].offset;
        line = postings[i].line;
    }
    return entries;
}

} // namespace

QByteArray encode(QList<Posting> postings)
{
    std::ranges::sort(postings, [](const Posting &a, const Posting &b) {
        return a.wordId != b.wordId ? a.wordId < b.wordId : a.offset < b.offset;
    });
    QByteArray encoded;
    std::uint32_t previousWordId = 0;
    for (qsizetype first = 0; first < postings.size();) {
        qsizetype last = first + 1;
        while (last < postings.size() && postings[last].wordId == postings[first].wordId)
            ++last;
        const QByteArray entries = encodeGroup(postings, first, last);
        appendVarint(encoded, postings[first].wordId - previousWordId);
        appendVarint(encoded, std::uint32_t(last - first));
        appendVarint(encoded, std::uint32_t(entries.size()));
        encoded += entries;
        previousWordId = postings[first].wordId;
        first = last;
    }
    return encoded;
}

void decode(const QByteArray &encoded, const std::function<bool(const Posting &)> &visit)
{
    VarintReader reader(encoded);
    std::uint32_t wordId = 0;
    while (!reader.atEnd()) {
        const GroupHeader header = readHeader(reader, wordId);
        if (!readGroup(reader, header, visit))
            return;
        wordId = header.wordId;
    }
}

QList<Posting> find(const QByteArray &encoded, std::uint32_t wordId)
{
    QList<Posting> found;
    VarintReader reader(encoded);
    std::uint32_t previous = 0;
    while (!reader.atEnd()) {
        const GroupHeader header = readHeader(reader, previous);
        previous = header.wordId;
        if (header.wordId < wordId) {
            reader.skip(header.size);
            continue;
        }
        if (header.wordId == wordId)
            readGroup(reader, header, [&found](const Posting &posting) {
                found.append(posting);
                return true;
            });
        break; // the groups come by increasing id
    }
    return found;
}

QList<std::uint32_t> wordIds(const QByteArray &encoded)
{
    QList<std::uint32_t> ids;
    VarintReader reader(encoded);
    std::uint32_t previous = 0;
    while (!reader.atEnd()) {
        const GroupHeader header = readHeader(reader, previous);
        previous = header.wordId;
        ids.append(header.wordId);
        reader.skip(header.size);
    }
    return ids;
}

} // namespace PostingCodec
