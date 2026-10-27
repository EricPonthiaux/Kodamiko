// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QIcon>
#include <QList>
#include <QObject>
#include <QSet>

class DiffView;
class QFrame;
class QLabel;
class QToolButton;
struct Theme;

// The actions on each block of changes in view, as buttons at the right of
// its separator (at the top of the view for the block whose separator has
// scrolled away): Stage, Discard and Ask for an unstaged change, Unstage and
// Ask for a staged one; Ask has the agent explain the block. A block of a
// file Claude edited carries Claude's mark before them. They look like the
// view mode selector of the HTML and Markdown previews. The context menu has
// the same actions, and those on selected lines.
class HunkActions : public QObject {
    Q_OBJECT

public:
    // Over `diff`, which owns it.
    explicit HunkActions(DiffView *diff);

    // The view's files (their index in DiffView::setFiles()) whose changes
    // are staged: their blocks get Unstage rather than Stage and Discard.
    void setStagedFiles(const QSet<int> &files);
    // The view's files Claude edited: their blocks carry its mark.
    void setClaudeFiles(const QSet<int> &files);
    void setEnabled(bool enabled);
    void applyTheme(const Theme &theme);

    // The separators whose buttons show, top to bottom (the block at the
    // top of the view first when its separator has scrolled away).
    QList<int> shownBlocks() const;

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    // On the block whose separator is displayed line `line`: stage it, or
    // unstage it when staged.
    void applyRequested(int line);
    void discardRequested(int line);
    void askRequested(int line);

private:
    // The buttons of one block.
    struct Bar {
        QFrame *frame = nullptr;
        QLabel *claudeMark = nullptr;
        QToolButton *apply = nullptr;
        QToolButton *discard = nullptr;
        QToolButton *ask = nullptr;
        int hunkLine = -1; // the separator of the block they act on
    };

    // Bar `index`, created when first needed.
    Bar &barAt(qsizetype index);
    // A bar on each block in view; the others hidden.
    void placeBars();
    // The blocks in view: their separators, and the top of each bar.
    QList<std::pair<int, int>> blocksInView() const;
    void placeBar(Bar &bar, int hunkLine, int top);
    // Its buttons for a staged block, or for an unstaged one.
    void updateButtons(Bar &bar, bool isStaged);
    void styleBar(Bar &bar);

    DiffView *diff_;
    QList<Bar> bars_;
    QIcon stageIcon_;
    QIcon unstageIcon_;
    QIcon discardIcon_;
    QIcon askIcon_;
    QColor claudeColor_;
    QSet<int> stagedFiles_;
    QSet<int> claudeFiles_;
    bool isEnabled_ = true;
};
