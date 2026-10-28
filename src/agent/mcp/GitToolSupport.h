// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "KDKTools.h"
#include "McpRegistry.h"
#include "git/CommitLog.h"
#include "git/GitCommands.h"
#include "git/RepositoryStatus.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// What the git tools share: git's data as JSON for Claude, and running git
// with the editor's other git work.
namespace GitToolSupport {

// A patch as Claude reads it: cut beyond a length Claude can read whole.
QString truncatedPatch(const QByteArray &bytes);
// The files of a diff (path, change, former path), with their patches when
// `patches` is set.
QJsonArray diffsJson(const QList<FileDiff> &diffs, bool patches);
QJsonObject commitJson(const CommitSummary &commit);

// Runs a git job with the editor's other git work (one at a time, off the
// UI thread), then tells the editor the repository changed and replies with
// the job's message, or its GitError.
void runGit(const KDKToolContext &context, GitCommands::Job job, McpReply reply);
// Same for a job that talks to a remote: git may ask the user for
// credentials (see AskpassServer).
void runRemoteGit(const KDKToolContext &context, GitCommands::LongJob job, McpReply reply);

// The project's repository; throws GitError when the project is in none.
RepositoryInfo repositoryOf(const QString &folder);
// The "paths" argument, relative to the project.
QStringList pathsArgument(const KDKToolContext &context, const QJsonObject &arguments);
// The "path" argument, relative to the project.
QString relativePathArgument(const KDKToolContext &context, const QJsonObject &arguments);

} // namespace GitToolSupport
