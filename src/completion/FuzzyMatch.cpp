// SPDX-License-Identifier: GPL-3.0-only

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
    // Separators, and the "::" or "." of a qualified name ("Foo::size").
    if (previous == QLatin1Char('_') || previous == QLatin1Char('-') || previous == QLatin1Char('$') ||
        previous == QLatin1Char(':') || previous == QLatin1Char('.') || previous == QLatin1Char('~') ||
        previous == QLatin1Char(' '))
        return current != previous;
    if (current.isUpper() && previous.isLower())
        return true;
    if (current.isLetter() && previous.isDigit())
        return true;
    return false;
}

constexpr int noMatch = INT_MIN / 2;

// Most candidates fail here, before any allocation: the letters in order.
bool hasLettersInOrder(const QString &pattern, const QString &candidate)
{
    const int n = int(pattern.size());
    const int m = int(candidate.size());
    for (int i = 0, j = 0; i < n; ++j) {
        if (j == m)
            return false;
        if (pattern[i].toLower() == candidate[j].toLower())
            ++i;
    }
    return true;
}

// Per-character facts and the scoring tables, reused from one candidate to
// the next (the list is filtered at every keystroke).
struct Buffers {
    std::vector<QChar> lowerPattern;
    std::vector<QChar> lowerCandidate;
    std::vector<char> wordStart;
    std::vector<int> bestCells;
    std::vector<int> fromCells;
};

// best(i, j): best score with pattern[0..i] matched, pattern[i] on
// candidate[j]; dynamic programming to prefer word starts and runs.
class Alignment {
public:
    Alignment(const QString &pattern, const QString &candidate, Buffers &buffers)
        : pattern_(pattern)
        , candidate_(candidate)
        , n_(int(pattern.size()))
        , m_(int(candidate.size()))
        , buffers_(buffers)
    {
        // Per-character facts, computed once rather than in the n×m loops.
        buffers_.lowerPattern.resize(size_t(n_));
        for (int i = 0; i < n_; ++i)
            buffers_.lowerPattern[size_t(i)] = pattern_[i].toLower();
        buffers_.lowerCandidate.resize(size_t(m_));
        buffers_.wordStart.resize(size_t(m_));
        for (int j = 0; j < m_; ++j) {
            buffers_.lowerCandidate[size_t(j)] = candidate_[j].toLower();
            buffers_.wordStart[size_t(j)] = isWordStart(candidate_, j);
        }
        buffers_.bestCells.assign(size_t(n_) * size_t(m_), noMatch);
        buffers_.fromCells.assign(size_t(n_) * size_t(m_), -1);
    }

    void scoreFirstLetter(bool anywhere)
    {
        for (int j = 0; j < m_; ++j) {
            // The first letter: at the start of a word only, unless anywhere.
            if (!isWordStartAt(j) && !anywhere)
                continue;
            const int score = charScore(0, j);
            if (score != noMatch)
                best(0, j) = score + (j == 0 ? 12 : 0);
        }
    }

    void scoreLetter(int i)
    {
        // Best best(i - 1, k) + k for k < j - 1: with a gap of j - k - 1
        // characters, best(i - 1, k) - (j - k - 1) = (best(i - 1, k) + k) + 1 - j.
        int bestBefore = noMatch;
        int bestBeforeAt = -1;
        for (int j = i; j < m_; ++j) {
            const bool improvesBestBefore =
                j >= 2 && best(i - 1, j - 2) != noMatch && best(i - 1, j - 2) + (j - 2) > bestBefore;
            if (improvesBestBefore) {
                bestBefore = best(i - 1, j - 2) + (j - 2);
                bestBeforeAt = j - 2;
            }
            scoreCell(i, j, bestBefore, bestBeforeAt);
        }
    }

    std::optional<Match> bestMatch()
    {
        int end = -1;
        int score = noMatch;
        for (int j = n_ - 1; j < m_; ++j) {
            if (best(n_ - 1, j) > score) {
                score = best(n_ - 1, j);
                end = j;
            }
        }
        if (end < 0)
            return std::nullopt;

        Match result;
        result.score = score;
        result.positions.resize(n_);
        for (int i = n_ - 1, j = end; i >= 0; --i) {
            result.positions[i] = j;
            j = from(i, j);
        }
        return result;
    }

private:
    const QString &pattern_;
    const QString &candidate_;
    const int n_;
    const int m_;
    Buffers &buffers_;

    int &best(int i, int j) { return buffers_.bestCells[size_t(i) * size_t(m_) + size_t(j)]; }

    int &from(int i, int j) { return buffers_.fromCells[size_t(i) * size_t(m_) + size_t(j)]; }

    bool isWordStartAt(int j) const { return buffers_.wordStart[size_t(j)] != 0; }

    int charScore(int i, int j) const
    {
        if (buffers_.lowerPattern[size_t(i)] != buffers_.lowerCandidate[size_t(j)])
            return noMatch;
        int score = 1;
        if (pattern_[i] == candidate_[j])
            score += 1; // same case
        if (isWordStartAt(j))
            score += 8;
        return score;
    }

    void scoreCell(int i, int j, int bestBefore, int bestBeforeAt)
    {
        const int score = charScore(i, j);
        if (score == noMatch)
            return;
        int value = noMatch;
        int origin = -1;
        if (best(i - 1, j - 1) != noMatch) { // consecutive: a bonus
            value = best(i - 1, j - 1) + score + 6;
            origin = j - 1;
        }
        if (bestBefore != noMatch) {
            // A gap: only worth it towards a word start.
            const int gapped = bestBefore + 1 - j + score - 2 - (isWordStartAt(j) ? 0 : 6);
            if (gapped > value) {
                value = gapped;
                origin = bestBeforeAt;
            }
        }
        best(i, j) = value;
        from(i, j) = origin;
    }
};

} // namespace

std::optional<Match> match(const QString &pattern, const QString &candidate, bool anywhere)
{
    if (pattern.isEmpty())
        return Match{};
    const int n = int(pattern.size());
    const int m = int(candidate.size());
    if (n > m)
        return std::nullopt;
    if (!hasLettersInOrder(pattern, candidate))
        return std::nullopt;

    thread_local Buffers buffers;
    Alignment alignment(pattern, candidate, buffers);
    alignment.scoreFirstLetter(anywhere);
    for (int i = 1; i < n; ++i)
        alignment.scoreLetter(i);
    std::optional<Match> result = alignment.bestMatch();
    if (!result)
        return std::nullopt;

    // A prefix ranks first; a shorter candidate before a longer one.
    const bool prefix = candidate.startsWith(pattern, Qt::CaseInsensitive);
    result->score += (prefix ? 40 : 0) + (candidate.startsWith(pattern) ? 10 : 0) - (m - n) / 3;
    return result;
}

} // namespace FuzzyMatch
