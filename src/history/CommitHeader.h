// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/CommitLog.h"
#include "ui/Theme.h"

#include <QWidget>

class QLabel;
class QTextBrowser;
class QToolButton;

// The chosen commit, above its diff in the history: its summary in
// demibold; the avatar, author and date, Claude's mark if Claude made it,
// and the ID with a copy button, on one line; the message (a few lines
// until expanded, then a third of the pane it heads at most), its other
// details and its branches and tags; the counts ("4 files changed +120
// −8"). A hint instead until a commit is chosen. Read only.
class CommitHeader : public QWidget {
    Q_OBJECT

public:
    explicit CommitHeader(QWidget *parent = nullptr);

    void showCommit(const CommitDetails &details);
    // Instead of a commit: what to choose, or why the commit can't be read.
    void showHint(const QString &hint);
    void applyTheme(const Theme &theme);

    // Whether a commit is shown, not a hint.
    bool showsCommit() const;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    // The message's body, its other details: a few lines, all of them once
    // expanded.
    QString messageHtml(const CommitDetails &details) const;
    // "4 files changed  +120 −8".
    QString statsHtml(const CommitDetails &details) const;
    void fitMessage();

    QString id_;       // of the commit shown, for the copy button
    QLabel *hint_;     // shown instead of the commit
    QWidget *commit_;  // the commit's lines
    QLabel *summary_;
    QLabel *avatar_;
    QLabel *meta_;
    QLabel *claudeMark_;
    QLabel *shortId_;
    QToolButton *copyId_;
    QTextBrowser *message_;
    QToolButton *moreMessage_;
    bool isMessageExpanded_ = false;
    QLabel *stats_;
};
