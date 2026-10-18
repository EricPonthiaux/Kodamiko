#pragma once

#include "git/RepositoryStatus.h"
#include "ui/Theme.h"

#include <QFrame>

class CommitMessageGenerator;
class GitCommands;
class QCheckBox;
class QPlainTextEdit;
class QPushButton;

// The commit area, fixed at the bottom of the Diff mode's file list under a
// line: the message, Amend, Generate (Claude writes the message from the
// changes) and Commit. With nothing staged, Commit offers to stage every
// change first. Commits go through GitCommands.
class CommitPanel : public QFrame {
    Q_OBJECT

public:
    explicit CommitPanel(GitCommands *commands, QWidget *parent = nullptr);

    // The changes to commit, in `pathspec` (the open folder within the
    // repository); nullptr outside a repository. Not owned: call again
    // whenever it changes.
    void setStatus(const RepositoryStatus *status, const QString &pathspec);
    // Another folder: the message and Amend start over, a generation stops.
    void reset();
    void updateActions();
    void focusMessage();
    // Has Claude write the message; while it writes, stops it.
    void generateMessage();
    void applyTheme(const Theme &theme);

signals:
    void statusMessage(const QString &message);

private:
    void onAmendToggled(bool amend);
    void onGeneratingChanged(bool generating);
    void showGeneratedMessage(const QString &message);
    void commit();
    void runCommit(const QString &message, bool amend, bool stageEverything);

    GitCommands *commands_;
    const RepositoryStatus *status_ = nullptr;
    QString pathspec_;
    QPlainTextEdit *message_;
    QCheckBox *amend_;
    QPushButton *generateButton_;
    QPushButton *commitButton_;
    CommitMessageGenerator *generator_;
};
