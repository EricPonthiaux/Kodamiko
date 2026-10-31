// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QStringView>

#include <utility>
#include <vector>

// A language highlighted by one of Lexilla's lexers: its auxiliary files
// (JSON, CMake, Markdown, XML…) have no tree-sitter grammar.
struct LexillaLanguage {
    // Lexilla's name for the lexer ("json", "hypertext"…).
    const char *lexer = nullptr;
    // The theme's category ("comment", "string"…) of each of the lexer's
    // styles (SCE_…); the styles left out keep the default text's color.
    std::vector<std::pair<int, const char *>> styleCategories;
    // The lexer's word lists (SCI_SETKEYWORDS), in its order.
    std::vector<const char *> keywords;
};

namespace LexillaLanguages {

// The language `languageId` names, or nullptr when no lexer highlights it.
const LexillaLanguage *forLanguage(QStringView languageId);

} // namespace LexillaLanguages
