// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "editor/TextFile.h"

#include <QString>

// A file a refactoring's edit changes, as it is and as it would be.
struct RefactoredFile {
    enum class Kind {
        Modified,
        Created,
        Renamed,
        Deleted
    };

    Kind kind = Kind::Modified;
    QString path;         // once changed
    QString originalPath; // before: another path when renamed, empty when created
    QString before;
    QString after;
    LineEnding lineEnding = LineEnding::Lf;
    // Only text edits: the file can be left out of the refactoring (see
    // EditPreview::without()); a created, renamed or deleted file cannot.
    bool hasTextEditsOnly = true;
};
