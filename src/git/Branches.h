#pragma once

#include "RepositoryStatus.h"

#include <QStringList>

// Local branches: listing, naming and creating them.
namespace Branches {

// Why `name` cannot name a branch (git check-ref-format's rules), or an
// empty string when it can.
QString nameError(const QString &name);

// What the user typed, made into a branch name: spaces become "-", the
// characters git refuses are dropped ("Fix the login bug" → "Fix-the-login-bug").
QString sanitizedName(const QString &typed);

// Names of the repository's local branches ("main", "feature/x").
// Throws GitError.
QStringList local(const QString &workDir);

// Creates the branch `name` at HEAD; with `switchTo`, also makes it the
// current branch, keeping the changes of the working directory and the
// index. In a repository without commits, `switchTo` renames the branch to
// come. Synchronous: run it off the UI thread (see GitCommands). Throws
// GitError.
void create(const RepositoryInfo &repository, const QString &name, bool switchTo);

} // namespace Branches
