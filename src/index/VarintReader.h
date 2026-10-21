// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>

#include <cstdint>

// Appends `value` to `out` as a LEB128 varint, what VarintReader reads.
void appendVarint(QByteArray &out, std::uint32_t value);

// Reads LEB128 varints (7 bits a byte, the low ones first) and bytes from
// the front of a buffer, never past its end: past it, values are 0.
class VarintReader {
public:
    // `encoded` must outlive the reader.
    explicit VarintReader(const QByteArray &encoded);

    bool atEnd() const { return position_ >= size_; }

    std::uint32_t varint();
    std::uint8_t byte();
    void skip(qsizetype count);

private:
    const char *data_;
    qsizetype size_;
    qsizetype position_ = 0;
};
