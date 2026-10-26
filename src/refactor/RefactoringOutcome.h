// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "lsp/LspProtocol.h"

#include <QString>
#include <QStringList>

#include <functional>

// When a refactoring is shown in its preview before it is made.
enum class Review {
    WhenNeeded, // if it reaches comments and strings, or has conflicts
    Always,
};

// How a refactoring ended, for who asked for it (Claude's tools).
struct RefactoringOutcome {
    enum class Status {
        Applied,
        Cancelled, // in the preview
        Failed,
    };

    Status status = Status::Failed;
    LspProtocol::WorkspaceEdit edit; // what was made
    int filesChanged = 0;
    QStringList messages; // the errors, or the conflicts shown
};

using RefactoringDone = std::function<void(const RefactoringOutcome &outcome)>;

// A refactoring before it is made.
struct Proposal {
    QString title; // "Rename “total” to “sum”"
    LspProtocol::WorkspaceEdit edit;
    // In comments and strings (see RenameAnalysis).
    LspProtocol::WorkspaceEdit textualEdit;
    QStringList conflicts;
};
