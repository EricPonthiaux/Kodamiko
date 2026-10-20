// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "IOutlineProvider.h"

// Line-by-line providers, for files without a tree-sitter grammar.

// Markdown headings ("# Heading" and "===" / "---" underlines), outside code blocks.
class MarkdownOutline : public IOutlineProvider {
public:
    std::vector<OutlineSymbol> symbols(const QByteArray &text) override;
};

// CMake: function(), macro() and targets (add_executable, add_library...).
class CMakeOutline : public IOutlineProvider {
public:
    std::vector<OutlineSymbol> symbols(const QByteArray &text) override;
};
