// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/FileBlame.h"
#include "ui/Theme.h"

#include <QWidget>

#include <memory>

class CommitTrack;
class ISyntaxHighlighter;
class QLabel;
class QStackedWidget;
class ScintillaView;

// The lines of a file by age, in a file's history (Lines by Age): its
// commits on a track of time (CommitTrack), a line that says what is
// chosen, then the file at HEAD, colored as in the editor, each line marked
// at its left in the color of its commit's age, the commit named beside its
// first line: under the mouse, with a hand, the name stands out, and a click
// chooses the commit. A commit chosen, here or in the history, lights the
// lines it wrote. Read only.
class FileLens : public QWidget {
    Q_OBJECT

public:
    explicit FileLens(const Theme &theme, QWidget *parent = nullptr);
    ~FileLens() override;

    // `path`: the file's, for its language.
    void showBlame(const QString &path, FileBlame blame);
    // Instead of the lines: while they are read, or why they cannot be.
    void showMessage(const QString &message);
    // Lights the lines of commit `id` and scrolls to the first; empty:
    // none lit.
    void setChosenCommit(const QString &id);
    void applyTheme(const Theme &theme);

    // The line whose commit's name stands out, under the mouse; -1: none.
    int hoveredRun() const { return hoveredRun_; }

signals:
    // On the track, or on the name of a commit beside its lines.
    void commitClicked(const QString &id);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // The first line of the run of lines written by `line`'s commit.
    int runStart(int line) const;
    // "Oct 22  Fix the parser ": beside the first line of a run.
    QString commitName(int line) const;
    // The name of the run under `position`, in the commits' margin, stands out.
    void hoverAt(QPoint position);
    // `line`: the first of a run; -1: none.
    void setHoveredRun(int line);
    // An editor coloring `language`, in place of the one before.
    void createCode(const QString &language);
    void styleCode();
    void showLines();
    void markChosenLines();
    void showInfo();
    // For each commit of the blame, 0 (the oldest) to the newest age.
    QList<int> ages() const;

    Theme theme_;
    FileBlame blame_;
    QString language_;
    QString chosen_;
    int hoveredRun_ = -1; // see hoveredRun()
    CommitTrack *track_;
    QLabel *info_;
    QLabel *message_;
    QStackedWidget *pages_; // the message, or the lines
    QWidget *linesPage_;
    ScintillaView *code_ = nullptr;
    // Destroyed before the editor it colors.
    std::unique_ptr<ISyntaxHighlighter> highlighter_;
};
