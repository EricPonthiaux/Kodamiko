// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/FileChange.h"
#include "git/GitProcess.h"
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

private:
    using Operation = std::function<QString(const RepositoryInfo &repository)>;

    void run(const QString &title, Operation operation, std::function<bool(const GitError &)> onFailure = {});
    void runRemote(const QString &title,
                   std::function<QString(const RepositoryInfo &, const Git::Feedback &)> operation,
                   std::function<bool(const GitError &)> onFailure = {});
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
    bool onPullFailed(const GitError &error);
    bool onDeleteBranchFailed(const GitError &error, const QString &name);
    // Deletes the worktree that has the branch `name` checked out, then the
    // branch.
    void deleteBranchAndWorktree(const QString &name);

    GitCommands *commands_;
    QWidget *window_;
    std::function<QString()> folder_;
};
