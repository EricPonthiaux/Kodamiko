// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/Worktrees.h"
#include "ui/OverlayPopup.h"
#include "ui/Theme.h"

class PopupHeader;
class QLabel;
class QPushButton;

// Asked when the tab of an agent whose worktree holds unmerged work closes:
// what becomes of the worktree. Cancelling keeps the tab open. The tabs
// closed while a question is asked wait their turn, once each.
class CloseAgentPopup : public OverlayPopup {
    Q_OBJECT

public:
    enum class WorktreeFate {
        Keep,
        Delete,
        MergeAndDelete, // its changes committed, merged, then the worktree goes
    };

    CloseAgentPopup(const Theme &theme, QWidget *parent);

    // Asks for `worktree`, after the worktrees already queued; nothing if it
    // is one of them.
    void popup(const Worktree &worktree);
    // popup() without showing, for tests.
    void load(const Worktree &worktree);
    void applyTheme(const Theme &theme);
    // The agent's icon, from the next applyTheme().
    void setIconName(const QString &iconName);

    // The worktree asked for, null when there is none.
    const Worktree *asked() const;
    // The worktrees waiting after the one asked for.
    qsizetype waitingCount() const;

    // Cancels: leaves the worktree asked for as it is, its tab open. The next
    // question takes its place, or the popup hides. Emits cancelled().
    void next();

    QPushButton *mergeButton() const { return merge_; }

    QPushButton *keepButton() const { return keep_; }

    QPushButton *deleteButton() const { return delete_; }

signals:
    // The tab closes; `worktree` is the one asked for.
    void fateChosen(const Worktree &worktree, CloseAgentPopup::WorktreeFate fate);
    // The tab of `worktree` stays open.
    void cancelled(const Worktree &worktree);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void choose(WorktreeFate fate);
    // Drops the question asked: the next one takes its place, or the popup
    // hides.
    void askNext();
    void showFirst();

    // The first is asked for, the others wait.
    QList<Worktree> queue_;
    PopupHeader *header_;
    QLabel *text_;
    QPushButton *merge_;
    QPushButton *keep_;
    QPushButton *delete_;
};
