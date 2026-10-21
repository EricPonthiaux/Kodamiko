// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "EditTargets.h"
#include "RefactoredFile.h"
#include "lsp/LspProtocol.h"

#include <QList>

// Makes the changes of a refactoring's edit in memory, one after the
// other, following each file under the path it has at that point (a file
// renamed, then edited under its new name, stays one file). The texts are
// read on a file's first change: its editor's when open, else the file's.
class EditSimulation {
public:
    // `targets` must outlive the simulation.
    explicit EditSimulation(const EditTargets &targets);

    void apply(const LspProtocol::DocumentChange &change);

    // The files changed so far, in the order of their first change.
    const QList<RefactoredFile> &files() const { return files_; }

private:
    // The file at `path` now, if changed so far and not deleted.
    RefactoredFile *find(const QString &path);
    // Where the file at `path` is before the edit: under a folder the edit
    // renamed, at its old path.
    QString originOf(const QString &path) const;
    // The file at `path`, read on its first change; nullptr if unreadable.
    RefactoredFile *load(const QString &path);
    void editText(const LspProtocol::DocumentEdit &edit);
    void create(const QString &path);
    void rename(const LspProtocol::FileOperation &operation);
    void remove(const QString &path);

    EditTargets targets_;
    QList<RefactoredFile> files_;
};
