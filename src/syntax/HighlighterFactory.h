#pragma once

#include "ISyntaxHighlighter.h"

#include <QString>

#include <memory>

namespace HighlighterFactory {

// Highlighter adapté au langage (identifiant de LanguageRegistry) :
// tree-sitter pour C, C++, Java, JavaScript et TypeScript, lexer QScintilla
// pour les fichiers annexes, aucune coloration sinon.
std::unique_ptr<ISyntaxHighlighter> create(const QString &languageId);

} // namespace HighlighterFactory
