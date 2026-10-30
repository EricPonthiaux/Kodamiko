// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/FileChange.h"
#include "git/GitProcess.h"
#include "git/Integration.h"
#include "git/RepositoryStatus.h"

#include <QObject>

#include <functional>
#include <optional>

class GitCommands;
class QWidget;

// The repository operations the user starts from the menus and popups:
// exchanges with the remote, branches, stashes, merge and rebase. Each runs
// through GitCommands, off the UI thread; when git refuses for a reason the
// user can act on (local changes in the way, a rejected push…), a dialog
// offers the way out instead of the bare error.
class GitActions : public QObject {
    Q_OBJECT

public:
    // `window` parents the dialogs; `folder` returns the open folder.
    GitActions(GitCommands *commands, QWidget *window, std::function<QString()> folder, QObject *parent = nullptr);

    void fetch();
    void pull();
    void push();
    // Fetch, pull, then push what the remote lacks.
    void sync();

    // `remote`: `name` is a remote branch ("origin/x").
    void switchBranch(const QString &name, bool remote);
    void checkoutCommit(const QString &id);
    void renameBranch(const QString &name);
    void deleteBranch(const QString &name, bool remote);
    // Makes the current branch track `remoteBranch`.
    void trackBranch(const QString &remoteBranch);
    void merge(const QString &branch);
    void rebase(const QString &onto);

    void continueOperation();
    // Same finalization as Continue, with a reviewed merge commit message.
    void finishMerge(const QString &message);
    void abortOperation();
    void skipCommit();

    // Asks for a message, then puts the changes aside.
    void stashChanges();
    void applyStash(int index, bool drop);
    void dropStash(int index, const QString &message);

    // The repository of the open folder, or nothing (with a message in the
    // status bar) outside one.
    std::optional<RepositoryInfo> repository();

signals:
    // An operation changed the repository.
    void changed();
    void statusMessage(const QString &message);
    // The operation actually finished; resolving its last file is not enough.
    void integrationFinished(const Integration::Completion &completion);
    void integrationAborted();
    // An operation stopped on conflicts, with git's report: not a failure,
    // the files are left to resolve.
    void stoppedOnConflicts(const QString &title, const QString &report);

private:
    using Operation = std::function<QString(const RepositoryInfo &repository)>;
    using RemoteOperation = std::function<QString(const RepositoryInfo &, const Git::Feedback &)>;

    void run(const QString &title,
             Operation operation,
             std::function<bool(const GitError &)> onFailure = {},
             std::function<void()> onSuccess = {});
    // Captures the sides before Git removes its operation state; reports
    // success only after the whole merge/rebase/sequence has ended.
    void runIntegration(const QString &title, Integration::Completion completion, Operation operation);
    void runRemote(const QString &title,
                   RemoteOperation operation,
                   std::function<bool(const GitError &)> onFailure = {},
                   std::function<void()> onSuccess = {});
    // The open folder's name, under the popups' titles.
    QString location() const;
    // Popup asking to confirm `question`; `action` names the button. Runs
    // `onConfirmed` once the user confirms, nothing if they cancel.
    void confirm(const QString &title,
                 const QString &question,
                 const QString &action,
                 std::function<void()> onConfirmed);
    void checkoutCommitNow(const QString &id);
    // Renames the branch `name` to the name the user typed; an error
    // message when git cannot use it.
    QString renameBranchTo(const QString &name, const QString &typed);

    bool onSwitchFailed(const GitError &error, const QString &name, bool remote);
    bool onPushFailed(const GitError &error);
    // Tells the user the push went through: a popup, not only the status
    // bar, which is easily missed after a long wait.
    void onPushed();
    bool onPullFailed(const GitError &error);
    // When the remote refused the account git signed in with, offers to
    // forget the saved credentials and run `operation` again, so git asks
    // for a user name and a password.
    bool offerSignInAgain(const GitError &error,
                          const QString &title,
                          const RemoteOperation &operation,
                          const std::function<void()> &onSuccess);
    bool onDeleteBranchFailed(const GitError &error, const QString &name);
    // Deletes the worktree that has the branch `name` checked out, then the
    // branch.
    void deleteBranchAndWorktree(const QString &name);

    GitCommands *commands_;
    QWidget *window_;
    std::function<QString()> folder_;
};
