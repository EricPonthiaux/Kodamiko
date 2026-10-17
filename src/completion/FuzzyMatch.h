#pragma once

#include <QList>
#include <QString>

#include <optional>

// Matching what is typed against a candidate, as in IntelliJ: the letters
// of the pattern in order, the first one at the start of a word of the
// candidate ("gsv" matches "getSelectedValue" and "get_saved_value").
namespace FuzzyMatch {

struct Match {
    int score = 0;
    QList<int> positions; // matched characters of the candidate, to highlight them
};

// nullopt if `candidate` does not match `pattern`. An empty pattern
// matches everything with a score of 0. Higher scores for a prefix, a
// match with the same case, consecutive letters and word starts; lower for
// long candidates. `anywhere`: the first letter may also be inside a word
// ("get" matches "widget"), for extended completion.
std::optional<Match> match(const QString &pattern, const QString &candidate, bool anywhere = false);

} // namespace FuzzyMatch
