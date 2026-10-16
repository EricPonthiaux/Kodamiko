#pragma once

#include <tree_sitter/api.h>

#include <vector>

// Foldable regions of a tree-sitter syntax tree, as Scintilla fold levels.
// The rule is the same for every grammar: a node between brackets ("{",
// "[", "(") that spans several lines, a multi-line comment, and a
// preprocessor conditional (#if… #endif). The line of the opening bracket
// is the fold's header; the closing line stays visible ("} else {" folds
// both ways), except for a comment, which folds entirely.
namespace TreeSitterFolds {

// Scintilla level (SC_FOLDLEVELBASE + depth, with SC_FOLDLEVELHEADERFLAG on
// the header lines) of each of the `lineCount` lines of the document.
std::vector<int> levels(TSNode root, int lineCount);

} // namespace TreeSitterFolds
