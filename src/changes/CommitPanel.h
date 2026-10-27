// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/RepositoryStatus.h"
#include "ui/Theme.h"

#include <QFrame>

class GitCommands;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

// The commit area, at the bottom of the Diff mode's file column under a
// line, below the files it commits: the message's summary, with a meter of
// its length against git's 72 characters (red beyond); its body; Amend,
// Generate (the agent's session in the terminal writes the message from the
// changes) and Commit. Enter in the summary goes on to the body, Ctrl+Enter
// in either commits; the message is the summary, a blank line, then the
// body. Given more height, the body takes it. With nothing staged, Commit
// offers to stage every change first; when git does not know who commits,
// it asks for the name and email. Without changes, Commit, Generate and
// Amend are disabled. Commits go through GitCommands.
class CommitPanel : public QFrame {
    Q_OBJECT

public:
    explicit CommitPanel(GitCommands *gitCommands, QWidget *parent = nullptr);

    // The changes to commit, in `pathspec` (the open folder within the
    // repository); nullptr outside a repository. Not owned: call again
    // whenever it changes.
    void setStatus(const RepositoryStatus *status, const QString &pathspec);
    // Another folder: the message and Amend start over.
    void reset();
    void updateActions();
    void focusMessage();
    // Asks the agent's session for the message of the next commit
    // (messageRequested()); it comes back through showGeneratedMessage().
    void generateMessage();
    // Replaces the message with `message`, written by the agent: its first
    // line as the summary, the rest as the body. Ctrl+Z in either brings back
    // what it held before.
    void showGeneratedMessage(const QString &message);
    // The summary, then the body after a blank line; trimmed.
    QString message() const;
    void applyTheme(const Theme &theme);
    // The agent that writes the message: its name in the tooltip and the
    // messages, its icon on Generate from the next applyTheme().
    void setAgent(const QString &name, const QString &iconName);

    // Its minimum: a few lines of message, until the user gives it more.
    QSize sizeHint() const override;

signals:
    void statusMessage(const QString &message);
    // `request`, to type in the agent session's prompt.
    void messageRequested(const QString &request);
    // Review: a review of the staged changes.
    void reviewRequested();

private:
    // `message` split into the summary and the body, as Ctrl+Z can undo.
    void setMessage(const QString &message);
    void clearMessage();
    // "12 / 72", red beyond.
    void updateMeter();
    void onAmendToggled(bool amend);
    void commit();
    void runCommit(const QString &message, bool amend, bool stageEverything);
    // Git does not know who commits: asks for the name and email, saves
    // them, then commits again.
    void askIdentityThenCommit(const QString &message, bool amend, bool stageEverything);

    GitCommands *commands_;
    const RepositoryStatus *status_ = nullptr;
    QString pathspec_;
    QLineEdit *summary_;
    QLabel *summaryCount_;
    QProgressBar *summaryBar_; // the meter's track
    QPlainTextEdit *body_;
    QColor meterColor_;
    QColor overColor_;
    QColor trackColor_;
    QCheckBox *amendCheckbox_;
    QPushButton *generateButton_;
    QPushButton *reviewButton_;
    QString agentName_;
    QString agentIconName_;
    QPushButton *commitButton_;
};
