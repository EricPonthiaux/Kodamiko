#pragma once

#include "IOutlineProvider.h"

// Fournisseurs ligne à ligne, pour les fichiers sans grammaire tree-sitter.

// Titres Markdown (« # Titre » et soulignés « === » / « --- »), hors blocs de code.
class MarkdownOutline : public IOutlineProvider {
public:
    std::vector<OutlineSymbol> symbols(const QByteArray &text) override;
};

// CMake : function(), macro() et cibles (add_executable, add_library...).
class CMakeOutline : public IOutlineProvider {
public:
    std::vector<OutlineSymbol> symbols(const QByteArray &text) override;
};
