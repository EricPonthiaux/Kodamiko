// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "EditTargets.h"
#include "RefactoredFile.h"
#include "changes/DiffLayout.h"
#include "lsp/LspProtocol.h"

#include <QList>
#include <QSet>
#include <QString>

// What a refactoring's edit would change, worked out in memory without
// changing anything, to show it before it is made.
namespace EditPreview {

// The files `edit` changes, in the order of their first change; the
// texts are those of the editors when open, unsaved changes included.
// A folder renamed or deleted is listed without texts.
QList<RefactoredFile> simulate(const LspProtocol::WorkspaceEdit &edit, const EditTargets &targets);

// The changes as a DiffView shows them, paths relative to `root`.
QList<DiffFile> diffFiles(const QList<RefactoredFile> &changes, const QString &root);

// `edit` without the text edits of the files `excluded` (by path).
LspProtocol::WorkspaceEdit without(const LspProtocol::WorkspaceEdit &edit, const QSet<QString> &excluded);

} // namespace EditPreview
