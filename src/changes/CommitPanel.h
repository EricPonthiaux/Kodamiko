// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/RepositoryStatus.h"
#include "ui/Theme.h"

#include <QFrame>

class GitCommands;
class QCheckBox;
class QPlainTextEdit;
class QPushButton;

// The commit area, fixed at the bottom of the Diff mode's file list under a
// line: the message, Amend, Generate (the Claude session in the terminal
// writes the message from the changes) and Commit. With nothing staged, Commit offers to stage every
// change first. Commits go through GitCommands.
class CommitPanel : public QFrame {
    Q_OBJECT

public:
    explicit CommitPanel(GitCommands *commands, QWidget *parent = nullptr);

    // The changes to commit, in `pathspec` (the open folder within the
    // repository); nullptr outside a repository. Not owned: call again
    // whenever it changes.
    void setStatus(const RepositoryStatus *status, const QString &pathspec);
    // Another folder: the message and Amend start over.
    void reset();
    void updateActions();
    void focusMessage();
    // Asks the Claude session for the message of the next commit
    // (messageRequested()); it comes back through showGeneratedMessage().
    void generateMessage();
    // Replaces the message with `message`, written by Claude; Ctrl+Z brings
    // back the one before.
    void showGeneratedMessage(const QString &message);
    void applyTheme(const Theme &theme);

signals:
    void statusMessage(const QString &message);
    // `request`, to type in the Claude session's prompt.
    void messageRequested(const QString &request);

private:
    void onAmendToggled(bool amend);
    void commit();
    void runCommit(const QString &message, bool amend, bool stageEverything);

    GitCommands *commands_;
    const RepositoryStatus *status_ = nullptr;
    QString pathspec_;
    QPlainTextEdit *message_;
    QCheckBox *amend_;
    QPushButton *generateButton_;
    QPushButton *commitButton_;
};
