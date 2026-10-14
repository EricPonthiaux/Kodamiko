#pragma once

#include "IOutlineProvider.h"

#include <QByteArrayList>

#include <tree_sitter/api.h>

class HighlightQuery;

// Structure d'après une requête tree-sitter outline.scm (voir
// src/syntax/queries/README.md). Le texte est analysé en entier à chaque
// appel : quelques millisecondes, même pour un gros fichier.
class TreeSitterOutline : public IOutlineProvider {
public:
    // `blankedMacros` : macros sans argument remplacées par des espaces avant
    // l'analyse, quand elles la perturbent (Q_OBJECT suivi de « public: »
    // se lit comme un champ nommé « public »).
    TreeSitterOutline(const TSLanguage *language, const HighlightQuery &query, QByteArrayList blankedMacros = {});
    ~TreeSitterOutline() override;

    TreeSitterOutline(const TreeSitterOutline &) = delete;
    TreeSitterOutline &operator=(const TreeSitterOutline &) = delete;

    std::vector<OutlineSymbol> symbols(const QByteArray &text) override;

private:
    const HighlightQuery &query_;
    QByteArrayList blankedMacros_;
    TSParser *parser_;
    TSQueryCursor *cursor_;
};
