// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "lsp/LspProtocol.h"
#include "refactor/EditJournal.h"
#include "refactor/EditTargets.h"

#include <QStringList>

#include <functional>

class QObject;

// Carrying out the edit of a rename or a quick fix, as a server sends it.
namespace WorkspaceEditing {

struct Outcome {
    int filesChanged = 0;
    QStringList errors; // one sentence each, for the user
    // What was changed, to undo it; empty unless every change was made.
    EditJournal journal;
};

// Makes the changes of `edit`, in their order, all of them or none:
// - text edits in the open editors, one undo step per editor, left
//   unsaved; in the other files, on disk;
// - file operations on disk: the editors follow a renamed file (saved
//   first), a deleted one goes to the trash and its tabs are closed.
// Nothing is changed when a file is no longer the version the server
// computed its edits for; when a change fails, those already made are
// reverted.
[[nodiscard]] Outcome apply(const LspProtocol::WorkspaceEdit &edit, const EditTargets &targets);

using Applied = std::function<void(const Outcome &outcome)>;

// apply(), with the files that are not open read and written on a worker
// thread: the UI stays responsive during a refactoring of many files. The
// open editors change before the return; `applied` is called on the UI
// thread once the files are written, or the changes reverted, and never if
// `context` (not owned) is gone by then. An edit with file operations,
// whose order matters, is applied before the return, `applied` included.
// The targets must outlive `context`.
void applyInBackground(const LspProtocol::WorkspaceEdit &edit,
                       const EditTargets &targets,
                       const QObject *context,
                       Applied applied);

} // namespace WorkspaceEditing
