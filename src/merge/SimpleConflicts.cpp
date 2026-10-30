// SPDX-License-Identifier: GPL-3.0-only

#include "SimpleConflicts.h"

#include <QByteArray>
#include <QByteArrayView>

#include <cctype>

namespace {

using ThreeWayMerge::Chunk;
using ThreeWayMerge::ChunkKind;

// A line ending, as a token stands for it among the encoded lines: tokens
// are one per line there, so a token cannot hold the line ending itself.
constexpr QByteArrayView lineFeedToken = "\x01";
constexpr QByteArrayView crLfToken = "\x01\x02";

bool isWordByte(char byte)
{
    const auto value = static_cast<unsigned char>(byte);
    // Bytes from 0x80 on belong to UTF-8 letters: part of the word.
    return std::isalnum(value) != 0 || byte == '_' || value >= 0x80;
}

bool isBlank(char byte)
{
    return byte == ' ' || byte == '\t';
}

// The length of the token at `start` of `text`.
qsizetype tokenLength(const QByteArray &text, qsizetype start)
{
    const char first = text[start];
    if (first == '\n')
        return 1;
    if (first == '\r' && start + 1 < text.size() && text[start + 1] == '\n')
        return 2;
    qsizetype end = start + 1;
    if (isWordByte(first)) {
        while (end < text.size() && isWordByte(text[end]))
            ++end;
    } else if (isBlank(first)) {
        while (end < text.size() && isBlank(text[end]))
            ++end;
    }
    return end - start;
}

// `lines` as text with one token per line.
QByteArray encoded(const QByteArrayList &lines)
{
    const QByteArray text = lines.join();
    QByteArray tokens;
    for (qsizetype start = 0; start < text.size();) {
        const qsizetype length = tokenLength(text, start);
        const QByteArray token = text.mid(start, length);
        if (token == "\n")
            tokens += lineFeedToken;
        else if (token == "\r\n")
            tokens += crLfToken;
        else
            tokens += token;
        tokens += '\n';
        start += length;
    }
    return tokens;
}

QByteArray decoded(const QByteArrayList &tokenLines)
{
    QByteArray text;
    for (QByteArray token : tokenLines) {
        token.chop(1); // the line ending each token was given
        if (token == lineFeedToken)
            text += '\n';
        else if (token == crLfToken)
            text += "\r\n";
        else
            text += token;
    }
    return text;
}

QByteArray withoutWhitespace(const QByteArrayList &lines)
{
    QByteArray text = lines.join();
    text.removeIf([](char byte) { return isBlank(byte) || byte == '\n' || byte == '\r'; });
    return text;
}

// Each side's changes of the words, when none conflicts.
std::optional<QByteArray> mergedWords(const Chunk &conflict)
{
    const QList<Chunk> chunks =
        ThreeWayMerge::merge(encoded(conflict.base), encoded(conflict.current), encoded(conflict.incoming));
    QByteArrayList tokens;
    for (const Chunk &chunk : chunks) {
        switch (chunk.kind) {
        case ChunkKind::Conflict:
            return std::nullopt;
        case ChunkKind::Incoming:
            tokens += chunk.incoming;
            break;
        case ChunkKind::Unchanged:
        case ChunkKind::Current:
        case ChunkKind::BothSame:
            tokens += chunk.current;
            break;
        }
    }
    return decoded(tokens);
}

} // namespace

namespace SimpleConflicts {

std::optional<QByteArrayList> resolve(const ThreeWayMerge::Chunk &conflict)
{
    if (withoutWhitespace(conflict.current) == withoutWhitespace(conflict.incoming))
        return conflict.current;
    const std::optional<QByteArray> merged = mergedWords(conflict);
    if (!merged)
        return std::nullopt;
    return ThreeWayMerge::lines(*merged);
}

} // namespace SimpleConflicts
