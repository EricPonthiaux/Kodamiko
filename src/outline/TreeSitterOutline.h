#pragma once

#include "IOutlineProvider.h"

#include <QByteArrayList>

#include <tree_sitter/api.h>

class HighlightQuery;

// Outline from a tree-sitter outline.scm query (see
// src/syntax/queries/README.md). The whole text is parsed on each
// call: a few milliseconds, even for a large file.
class TreeSitterOutline : public IOutlineProvider {
public:
    // `blankedMacros`: argument-less macros replaced with spaces before
    // parsing, when they confuse it (Q_OBJECT followed by "public:"
    // reads as a field named "public").
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
