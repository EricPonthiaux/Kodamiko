// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

#include <optional>
#include <vector>

// The file operations done in the tree, undone last first (Ctrl+Z), as in
// VS Code and IntelliJ: a created or copied entry goes to the trash, a
// renamed or moved one goes back, one moved to the trash comes back from it.
// An entry deleted permanently cannot come back: its deletion is not
// recorded.
class FileUndoStack {
public:
    struct Step {
        enum class Kind {
            Created, // `path` created, or copied there
            Moved,   // `source` renamed or moved to `path`
            Trashed  // `path` moved to the trash, where it is `source`
        };
        Kind kind = Kind::Created;
        QString path;
        QString source;
    };

    void record(Step step);
    // The step that undo() reverts, nullopt if none.
    std::optional<Step> last() const;
    // What undo() would do, for a menu: “Undo Rename of “a.cpp””; empty if
    // there is nothing to undo.
    QString lastDescription() const;
    // Reverts the last step and forgets it, even when it fails (the files
    // changed since). Returns an error message, empty once undone.
    QString undo();

private:
    std::vector<Step> steps_;
};
