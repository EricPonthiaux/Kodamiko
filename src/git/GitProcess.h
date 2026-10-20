// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>
#include <QHash>
#include <QStringList>

#include <atomic>
#include <functional>

namespace Git {

// What a long operation (fetch, push…) reports while it runs, and how it is
// stopped. Handed to the jobs of GitCommands::runLong.
struct Feedback {
    // Called on the calling thread with each progress line git prints
    // ("Receiving objects:  42% (420/1000)"); null for none.
    std::function<void(const QString &line)> progress;
    // Polled while git runs: when it becomes true, git is killed and run()
    // throws a GitError of kind Cancelled.
    const std::atomic_bool *cancel = nullptr;
    // Added to git's environment: how to ask for credentials (see
    // AskpassServer).
    QHash<QString, QString> environment;
};

struct RunOptions {
    QByteArray input;                    // written to git's standard input
    QHash<QString, QString> environment; // added to the editor's
    Feedback feedback;
};

// Runs "git <arguments>" in `workDir`, waits for it to finish and returns its
// standard output. git never prompts for anything on a terminal: no message
// editor, no credential prompt (only through the feedback's askpass
// program), and paths passed after "--" are taken literally.
// Throws GitError (with git's error output) on failure.
QByteArray run(const QString &workDir, const QStringList &arguments, const RunOptions &options);
QByteArray run(const QString &workDir,
               const QStringList &arguments,
               const QByteArray &input = {},
               const QHash<QString, QString> &environment = {});

// Environment for the commands whose error messages are read (to tell a
// rejected push from other failures): git's messages in English, whatever
// the user's language.
QHash<QString, QString> englishMessages();

// Splits a "-z" output (fields terminated by a null byte).
QStringList splitNul(const QByteArray &output);

} // namespace Git
