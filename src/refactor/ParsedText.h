// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "lsp/LspProtocol.h"

#include <QByteArray>
#include <QString>

#include <tree_sitter/api.h>

#include <cstdint>
#include <memory>

// A text and its tree-sitter syntax tree, for the refactorings that change
// code in place: its nodes' texts, and positions both ways between bytes
// and the LSP's lines and UTF-16 columns. In C++, Qt's class macros are
// blanked and "signals:" read as "public:" for the parser, which they
// would confuse; the positions and texts stay those of the text.
class ParsedText {
public:
    // `languageId` is one of OccurrenceGrammars' ("cpp", "python"…).
    ParsedText(const QString &languageId, const QString &text);

    // False for a language without a grammar.
    bool isValid() const { return tree_ != nullptr; }

    TSNode root() const { return ts_tree_root_node(tree_.get()); }

    const QByteArray &bytes() const { return bytes_; }

    QString textOf(TSNode node) const;
    QString textOf(std::uint32_t start, std::uint32_t end) const;
    LspProtocol::Position positionAt(std::uint32_t offset) const;
    std::uint32_t offsetAt(const LspProtocol::Position &position) const;
    LspProtocol::Range rangeOf(TSNode node) const;

private:
    QByteArray bytes_; // UTF-8
    std::unique_ptr<TSTree, decltype(&ts_tree_delete)> tree_{nullptr, ts_tree_delete};
};
