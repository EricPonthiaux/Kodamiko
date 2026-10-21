// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "lsp/LspProtocol.h"

#include <QList>
#include <QString>

// How a usage uses the name, the Usages pane's filters.
enum class UsageKind {
    Declaration,
    Read,
    Write,
    Call,
    Import,
    Textual, // in a comment, a string or a file that is not code
};

// A place where a name is used.
struct Usage {
    QString path;
    int line = 0;   // from 0
    int column = 0; // UTF-16 code units from the start of the line
    int length = 0;
    QString lineText; // trimmed of its line ending
    UsageKind kind = UsageKind::Read;

    bool operator==(const Usage &) const = default;
};

// A search for usages, kept to be run again.
struct UsageSearch {
    QString name;
    QString path; // where the name was searched from
    LspProtocol::Position position;
};

// What a search found. `isSemantic`: from the language server, which
// knows the symbol; otherwise every occurrence of the word, from the
// project's index.
struct UsageResults {
    UsageSearch search;
    QList<Usage> usages;
    bool isSemantic = true;
};
