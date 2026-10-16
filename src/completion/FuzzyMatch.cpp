#include "FuzzyMatch.h"

#include <climits>
#include <vector>

namespace FuzzyMatch {
namespace {

// Starts of words: the first character, a capital after a small letter
// ("getValue"), a letter after "_" or a digit boundary.
bool isWordStart(const QString &text, int i)
{
    if (i == 0)
        return true;
    const QChar previous = text[i - 1];
    const QChar current = text[i];
    if (previous == QLatin1Char('_') || previous == QLatin1Char('-') || previous == QLatin1Char('$'))
        return current != QLatin1Char('_');
    if (current.isUpper() && previous.isLower())
        return true;
    if (current.isLetter() && previous.isDigit())
        return true;
    return false;
}

constexpr int noMatch = INT_MIN / 2;

} // namespace

std::optional<Match> match(const QString &pattern, const QString &candidate)
{
    if (pattern.isEmpty())
        return Match{};
    const int n = int(pattern.size());
    const int m = int(candidate.size());
    if (n > m)
        return std::nullopt;
    // Most candidates fail here, before any allocation: the letters in order.
    for (int i = 0, j = 0; i < n; ++j) {
        if (j == m)
            return std::nullopt;
        if (pattern[i].toLower() == candidate[j].toLower())
            ++i;
    }

    // best[i][j]: best score with pattern[0..i] matched, pattern[i] on
    // candidate[j]; dynamic programming to prefer word starts and runs.
    std::vector<std::vector<int>> best(n, std::vector<int>(m, noMatch));
    std::vector<std::vector<int>> from(n, std::vector<int>(m, -1));
    const auto charScore = [&](int i, int j) {
        if (pattern[i].toLower() != candidate[j].toLower())
            return noMatch;
        int score = 1;
        if (pattern[i] == candidate[j])
            score += 1; // same case
        if (isWordStart(candidate, j))
            score += 8;
        return score;
    };

    for (int j = 0; j < m; ++j) {
        // The first letter: at the start of a word only.
        if (!isWordStart(candidate, j))
            continue;
        const int score = charScore(0, j);
        if (score != noMatch)
            best[0][j] = score + (j == 0 ? 12 : 0);
    }
    for (int i = 1; i < n; ++i) {
        // Best best[i - 1][k] + k for k < j - 1: with a gap of j - k - 1
        // characters, best[i - 1][k] - (j - k - 1) = (best[i - 1][k] + k) + 1 - j.
        int bestBefore = noMatch;
        int bestBeforeAt = -1;
        for (int j = i; j < m; ++j) {
            if (j >= 2 && best[i - 1][j - 2] != noMatch && best[i - 1][j - 2] + (j - 2) > bestBefore) {
                bestBefore = best[i - 1][j - 2] + (j - 2);
                bestBeforeAt = j - 2;
            }
            const int score = charScore(i, j);
            if (score == noMatch)
                continue;
            int value = noMatch;
            int origin = -1;
            if (best[i - 1][j - 1] != noMatch) { // consecutive: a bonus
                value = best[i - 1][j - 1] + score + 6;
                origin = j - 1;
            }
            if (bestBefore != noMatch) {
                // A gap: only worth it towards a word start.
                const int gapped = bestBefore + 1 - j + score - 2 - (isWordStart(candidate, j) ? 0 : 6);
                if (gapped > value) {
                    value = gapped;
                    origin = bestBeforeAt;
                }
            }
            best[i][j] = value;
            from[i][j] = origin;
        }
    }

    int end = -1;
    int score = noMatch;
    for (int j = n - 1; j < m; ++j) {
        if (best[n - 1][j] > score) {
            score = best[n - 1][j];
            end = j;
        }
    }
    if (end < 0)
        return std::nullopt;

    Match result;
    result.positions.resize(n);
    for (int i = n - 1, j = end; i >= 0; --i) {
        result.positions[i] = j;
        j = from[i][j];
    }
    // A prefix ranks first; a shorter candidate before a longer one.
    const bool prefix = candidate.startsWith(pattern, Qt::CaseInsensitive);
    result.score = score + (prefix ? 40 : 0) + (candidate.startsWith(pattern) ? 10 : 0) - (m - n) / 3;
    return result;
}

} // namespace FuzzyMatch
