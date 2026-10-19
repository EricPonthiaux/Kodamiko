#include "WordDiff.h"

#include <algorithm>
#include <vector>

namespace {

// Beyond this many pairs of tokens, the table of commonLengths() costs more
// than the brighter words are worth.
constexpr size_t maxComparedTokenPairs = 40000;

QList<TextRange> tokenize(const QString &text)
{
    const auto isWord = [](QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); };
    QList<TextRange> tokens;
    for (int i = 0; i < text.size();) {
        int end = i + 1;
        if (isWord(text[i])) {
            while (end < text.size() && isWord(text[end]))
                ++end;
        } else if (text[i].isSpace()) {
            while (end < text.size() && text[end].isSpace())
                ++end;
        }
        tokens.append({i, end - i});
        i = end;
    }
    return tokens;
}

int visibleLength(QStringView token)
{
    return static_cast<int>(std::count_if(token.begin(), token.end(), [](QChar c) { return !c.isSpace(); }));
}

QList<TextRange> changedRanges(const QString &text, const QList<TextRange> &tokens, std::vector<bool> kept)
{
    for (size_t i = 1; i + 1 < kept.size(); ++i) {
        const TextRange token = tokens[static_cast<int>(i)];
        if (kept[i] && !kept[i - 1] && !kept[i + 1] && text[token.start].isSpace())
            kept[i] = false;
    }
    QList<TextRange> ranges;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (kept[i])
            continue;
        const TextRange token = tokens[static_cast<int>(i)];
        if (!ranges.isEmpty() && ranges.last().start + ranges.last().length == token.start)
            ranges.last().length += token.length;
        else
            ranges.append(token);
    }
    return ranges;
}

QStringView tokenText(const QString &text, TextRange token)
{
    return QStringView(text).mid(token.start, token.length);
}

std::vector<int> commonLengths(const QString &removed,
                               const QList<TextRange> &before,
                               const QString &added,
                               const QList<TextRange> &after)
{
    const size_t n = before.size();
    const size_t m = after.size();
    std::vector<int> common((n + 1) * (m + 1), 0);
    const auto at = [m](size_t i, size_t j) { return i * (m + 1) + j; };
    for (size_t i = n; i-- > 0;) {
        for (size_t j = m; j-- > 0;) {
            const bool isSame = tokenText(removed, before[int(i)]) == tokenText(added, after[int(j)]);
            common[at(i, j)] =
                isSame ? common[at(i + 1, j + 1)] + 1 : std::max(common[at(i + 1, j)], common[at(i, j + 1)]);
        }
    }
    return common;
}

} // namespace

namespace WordDiff {

std::pair<QList<TextRange>, QList<TextRange>> changes(const QString &removed, const QString &added)
{
    const QList<TextRange> before = tokenize(removed);
    const QList<TextRange> after = tokenize(added);
    const size_t n = before.size();
    const size_t m = after.size();
    if (n == 0 || m == 0 || n * m > maxComparedTokenPairs)
        return {};

    const std::vector<int> common = commonLengths(removed, before, added, after);
    const auto at = [m](size_t i, size_t j) { return i * (m + 1) + j; };
    std::vector<bool> keptBefore(n, false);
    std::vector<bool> keptAfter(m, false);
    int shared = 0;
    for (size_t i = 0, j = 0; i < n && j < m;) {
        if (tokenText(removed, before[int(i)]) == tokenText(added, after[int(j)])) {
            keptBefore[i] = keptAfter[j] = true;
            shared += visibleLength(tokenText(removed, before[int(i)]));
            ++i;
            ++j;
        } else if (common[at(i + 1, j)] >= common[at(i, j + 1)]) {
            ++i;
        } else {
            ++j;
        }
    }
    // Mostly rewritten: brighter words everywhere would only add noise.
    const int total = visibleLength(removed) + visibleLength(added);
    if (total == 0 || 2 * shared < total / 2)
        return {};
    return {changedRanges(removed, before, keptBefore), changedRanges(added, after, keptAfter)};
}

} // namespace WordDiff
