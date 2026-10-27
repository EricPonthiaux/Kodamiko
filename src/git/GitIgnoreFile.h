// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>
#include <QStringList>

// The .gitignore file at the root of a repository's working directory, as the
// "Ignore" command of the project tree writes it.
namespace GitIgnoreFile {

enum class EntryKind {
    File,
    Folder
};

// Line that ignores exactly `repositoryPath` ("src/build", relative to the
// working directory): anchored at the root ("/src/build"), followed by "/" for
// a folder, with git's wildcards and trailing spaces escaped.
QString patternFor(const QString &repositoryPath, EntryKind kind);

// Appends to `workDir`/.gitignore the `patterns` it does not hold yet, creating
// the file if needed, in its line endings. Returns false with a message in
// `error` if the file cannot be read or written.
[[nodiscard]] bool add(const QString &workDir, const QStringList &patterns, QString *error);

} // namespace GitIgnoreFile
