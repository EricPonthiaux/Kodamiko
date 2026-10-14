#pragma once

#include "OutlineSymbol.h"

#include <QByteArray>

#include <vector>

// Calcule la structure d'un fichier (voir OutlineProviders::create). Chaque
// volet Structure possède son fournisseur, choisi selon le langage du fichier.
class IOutlineProvider {
public:
    virtual ~IOutlineProvider() = default;

    // Structure de `text`, texte UTF-8 complet du fichier.
    virtual std::vector<OutlineSymbol> symbols(const QByteArray &text) = 0;
};
