// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ISyntaxHighlighter.h"

#include <QString>

#include <memory>

namespace HighlighterFactory {

// Highlighter suited to the language (LanguageRegistry identifier):
// tree-sitter for C, C++, Java, JavaScript and TypeScript, a QScintilla lexer
// for auxiliary files, no highlighting otherwise.
std::unique_ptr<ISyntaxHighlighter> create(const QString &languageId);

} // namespace HighlighterFactory
