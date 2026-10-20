// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "FileChange.h"
#include "GitProcess.h"

#include <QObject>
#include <QThreadPool>

#include <atomic>
#include <functional>
#include <optional>

// Queue of git operations that modify the repository (see GitOperations). They
// run one at a time, in order, off the UI thread: a slow commit hook does not
// freeze the editor.
class GitCommands : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(GitCommands)

public:
    // Runs off the UI thread: must not touch any widget.
    // Returns the message to show on success; throws GitError.
    using Job = std::function<QString()>;
    // A job that talks to a remote (fetch, push…): it passes the feedback
    // to Git::run, for its progress, its cancellation and the credential
    // prompts.
    using LongJob = std::function<QString(const Git::Feedback &feedback)>;
    // Runs on the UI thread when a job fails; returns true when it handled
    // the error itself (offered a way out…): failed() is then not emitted.
    using FailureHandler = std::function<bool(const GitError &error)>;

    explicit GitCommands(QObject *parent = nullptr);
    // Waits for pending operations to finish.
    ~GitCommands() override;

    // `title` describes the operation ("Commit", "Staging"…). `onSuccess`
    // runs on the UI thread if the operation succeeds.
    void run(const QString &title, Job job, std::function<void()> onSuccess = {}, FailureHandler onFailure = {});
    // Same, for an operation the user may wait on: progress() reports its
    // progress and cancel() stops it.
    void runLong(const QString &title,
                 LongJob job,
                 std::function<void()> onSuccess = {},
                 FailureHandler onFailure = {});
    // Same queue, for background work the user did not ask for: no busy
    // state, no succeeded()/failed(). `done` runs on the UI thread with the
    // job's message, or its error (empty on success).
    void runQuietly(Job job, std::function<void(const QString &message, const QString &error)> done);
    // Same as runQuietly(), for a job that talks to a remote (periodic
    // fetch…). It is not cancelled by cancel().
    void runLongQuietly(LongJob job, std::function<void(const QString &message, const QString &error)> done);
    // Stops the long operation that is running, if any.
    void cancel();

    bool isBusy() const { return pending_ > 0; }

    // Added to git's environment for the long jobs: the askpass program
    // that asks the user for credentials (see AskpassServer).
    void setRemoteEnvironment(const QHash<QString, QString> &environment) { remoteEnvironment_ = environment; }

signals:
    void busyChanged(bool busy);
    // A long operation started (with an empty line) or reported progress.
    void progress(const QString &title, const QString &line);
    void succeeded(const QString &title, const QString &message);
    void failed(const QString &title, const QString &error);

private:
    using Done = std::function<void(const QString &message, const std::optional<GitError> &error)>;

    void start(Job job, Done done);
    Done finishing(const QString &title, std::function<void()> onSuccess, FailureHandler onFailure);
    Git::Feedback feedback(const QString &title, bool cancellable);

    QThreadPool pool_;
    int pending_ = 0;
    std::atomic_bool cancelled_ = false;
    std::atomic_bool neverCancelled_ = false;
    QHash<QString, QString> remoteEnvironment_;
};
