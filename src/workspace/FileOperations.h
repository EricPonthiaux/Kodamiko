// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

// Copying and moving files and folders for the file tree. Nothing is ever
// overwritten: the destination must not exist.
namespace FileOperations {

// Copies a file, or a folder with everything it contains. Returns an error
// message, empty on success; a failed copy leaves nothing behind.
QString copy(const QString &source, const QString &destination);
// Moves (renames) a file or folder; across file systems, copies then
// deletes. Returns an error message, empty on success.
QString move(const QString &source, const QString &destination);

// Whether `path` is `folder` or inside it: a folder cannot be moved or
// copied into itself.
bool isInside(const QString &path, const QString &folder);
// A name for a copy of `name` in `folder` that does not exist yet:
// "Foo_copy.cpp", then "Foo_copy2.cpp"…
QString copyName(const QString &folder, const QString &name);
// The name itself if it is free in `folder`, otherwise "Foo (2).cpp"…
QString freeName(const QString &folder, const QString &name);

} // namespace FileOperations
