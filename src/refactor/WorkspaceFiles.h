// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "EditTargets.h"

#include <QString>

#include <optional>

// The project's files as a refactoring sees them: the text of a file is
// its editor's when it is open, unsaved changes included, else the file's.
// Functions that change something return an error message, empty on
// success.
namespace WorkspaceFiles {

// nullopt if the file cannot be read.
std::optional<QString> textOf(const QString &path, const EditTargets &targets);
// In its editor, as one undo step, left unsaved; else on disk, in the
// file's encoding and line endings.
QString setText(const QString &path, const QString &text, const EditTargets &targets);

// Renames or moves a file or folder: its open editors are saved first,
// then follow it; the servers read it again from its new path.
QString move(const QString &from, const QString &to, const EditTargets &targets);
// Moves `path` to the trash and closes its editors; `pathInTrash` tells
// where it went, empty when the system does not say.
QString trash(const QString &path, const EditTargets &targets, QString *pathInTrash);
// Deletes the file `path` for good and closes its editors.
QString remove(const QString &path, const EditTargets &targets);
// Creates an empty file, and the folders leading to it.
QString createEmpty(const QString &path);

// Whether `path` is `folder` itself or inside it.
bool isUnder(const QString &path, const QString &folder);

} // namespace WorkspaceFiles
