// SPDX-License-Identifier: GPL-3.0-only

#include "VarintReader.h"

#include <algorithm>

namespace {

constexpr std::uint32_t payloadBits = 7;
constexpr std::uint8_t payloadMask = 0x7f;
constexpr std::uint8_t continues = 0x80;

} // namespace

VarintReader::VarintReader(const QByteArray &encoded)
    : data_(encoded.constData())
    , size_(encoded.size())
{
}

std::uint32_t VarintReader::varint()
{
    std::uint32_t value = 0;
    for (std::uint32_t shift = 0; position_ < size_; shift += payloadBits) {
        const auto byte = static_cast<std::uint8_t>(data_[position_++]);
        value |= std::uint32_t(byte & payloadMask) << shift;
        if ((byte & continues) == 0)
            break;
    }
    return value;
}

std::uint8_t VarintReader::byte()
{
    return position_ < size_ ? static_cast<std::uint8_t>(data_[position_++]) : 0;
}

void VarintReader::skip(qsizetype count)
{
    position_ = std::min(size_, position_ + count);
}

void appendVarint(QByteArray &out, std::uint32_t value)
{
    while (value >= continues) {
        out.append(char((value & payloadMask) | continues));
        value >>= payloadBits;
    }
    out.append(char(value));
}
