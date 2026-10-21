// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "lsp/LspProtocol.h"

#include <QStringList>

class EditorTabs;
class LanguageServers;

// Carrying out the edit of a rename or a quick fix, as a server sends it.
namespace WorkspaceEditing {

// What an edit changes; none is owned.
struct Targets {
    EditorTabs *tabs = nullptr;
    LanguageServers *servers = nullptr;
};

struct Outcome {
    int filesChanged = 0;
    QStringList errors; // one sentence each, for the user
};

// Makes the changes of `edit`, in their order:
// - text edits in the open editors, one undo step per editor, left
//   unsaved; in the other files, on disk;
// - file operations on disk: the editors follow a renamed file (saved
//   first), a deleted one goes to the trash and its tabs are closed.
// Nothing is changed when a file is no longer the version the server
// computed its edits for. A change that fails is reported in the errors;
// the others are still made.
[[nodiscard]] Outcome apply(const LspProtocol::WorkspaceEdit &edit, const Targets &targets);

} // namespace WorkspaceEditing
