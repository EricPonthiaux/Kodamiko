// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>
#include <QString>

#include <utility>

// Characters of a line's text, from `start`.
struct TextRange {
    int start = 0;
    int length = 0;
    bool operator==(const TextRange &) const = default;
};

namespace WordDiff {

// Words that differ between a removed line and the line added in its place,
// in each of them; nothing when they have too little in common for it to
// help.
std::pair<QList<TextRange>, QList<TextRange>> changes(const QString &removed, const QString &added);

} // namespace WordDiff
