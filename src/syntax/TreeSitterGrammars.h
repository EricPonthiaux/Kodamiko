#pragma once

#include <QString>

#include <tree_sitter/api.h>

class HighlightQuery;

struct TreeSitterGrammar {
    const TSLanguage *language;
    const HighlightQuery &highlights;
    const HighlightQuery *outline; // requête du volet Structure, nullptr si absente ou invalide
};

namespace TreeSitterGrammars {

// Grammaire et requêtes (coloration, structure) du langage, ou nullptr si le
// langage n'a pas de grammaire tree-sitter embarquée. Les requêtes sont
// compilées au premier appel puis conservées.
const TreeSitterGrammar *forLanguage(const QString &languageId);

} // namespace TreeSitterGrammars
