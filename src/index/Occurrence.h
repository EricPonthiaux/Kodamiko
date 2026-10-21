// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>

#include <cstdint>

// What a word is, where it occurs (see src/index/queries/README.md).
enum class OccurrenceRole : std::uint8_t {
    Reference, // an identifier in the code
    Definition,
    Call,
    Write,
    Import,  // a word of an include or an import
    Comment, // a word of a comment
    String,  // a word of a string literal
    Text,    // a word of a file that is not code
};

// A word found in a file: its UTF-8 bytes, where it starts (in bytes, and
// its line from 0) and its role.
struct Occurrence {
    QByteArray word;
    std::uint32_t offset = 0;
    std::uint32_t line = 0;
    OccurrenceRole role = OccurrenceRole::Reference;

    bool operator==(const Occurrence &) const = default;
};
