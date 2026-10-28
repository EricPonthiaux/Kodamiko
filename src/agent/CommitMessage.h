// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

// Commit message written by the agent session in Kodamiko's terminal: Kodamiko
// types the request in its prompt, the agent reads the changes with its own
// tools and hands the message back with the set_commit_message tool. The
// session knows why the changes were made, which the diff does not say.
namespace CommitMessage {

// What the next commit will hold, as CommitPanel decides it.
enum class Scope {
    Staged,     // the index
    Everything, // nothing staged: every change under the pathspec
    Amend       // the last commit with the index
};

// The request typed in the session's prompt, for `scope`; `pathspec` (the
// open folder within the repository, empty for all of it) for Everything.
QString request(Scope scope, const QString &pathspec);

// A message handed by the agent, as git expects it: without the fences or
// quotes the agent may add, the body's long lines wrapped at 72 columns.
QString tidy(const QString &message);

} // namespace CommitMessage
