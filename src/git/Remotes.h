// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "GitProcess.h"
#include "RepositoryStatus.h"

#include <QStringList>

// Exchanges with the remote repositories: fetch, pull, push. Synchronous
// and slow (network): run them off the UI thread, with the feedback of
// GitCommands::runLong. Each returns the message to show and throws
// GitError.
namespace Remotes {

// The repository's remotes ("origin"…). Throws GitError.
QStringList names(const QString &workDir);

// The remote a branch without upstream is pushed to: "origin", or the only
// remote; empty when there is none, or several and no "origin".
QString defaultRemote(const QStringList &remotes);

// Updates the remote branches of every remote, and forgets those deleted
// there.
QString fetch(const RepositoryInfo &repository, const Git::Feedback &feedback);

// Brings the upstream's commits into the current branch. Follows the
// user's pull.rebase and pull.ff; without either, only fast-forwards, and
// fails with GitError::Kind::Diverged when both sides have new commits.
QString pull(const RepositoryInfo &repository, const Git::Feedback &feedback);

// Sends the current branch to its upstream; a branch without upstream is
// pushed to the default remote under the same name, and tracks it from
// then on. Fails with GitError::Kind::Rejected when the remote branch has
// commits the local one lacks; `force` then overwrites them, unless
// someone pushed others since the last fetch (--force-with-lease).
QString push(const RepositoryInfo &repository, const Git::Feedback &feedback, bool force = false);

// Fetch, then pull if the upstream has new commits, then push if the
// branch has commits the upstream lacks.
QString sync(const RepositoryInfo &repository, const Git::Feedback &feedback);

} // namespace Remotes
