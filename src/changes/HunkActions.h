#pragma once

#include <QIcon>
#include <QObject>

class DiffView;
class QFrame;
class QToolButton;
struct Theme;

// The actions on a block of changes, as buttons at the right of its
// separator while the mouse is over the block (at the top of the view when
// its separator has scrolled away): Stage and Discard for an unstaged
// change, Unstage for a staged one. They look like the view mode selector
// of the HTML and Markdown previews. The context menu has the same
// actions, and those on selected lines.
class HunkActions : public QObject {
    Q_OBJECT

public:
    // Over `diff`, which owns it.
    explicit HunkActions(DiffView *diff);

    // Unstage rather than Stage and Discard.
    void setStaged(bool staged);
    void setEnabled(bool enabled);
    void applyTheme(const Theme &theme);

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    // On the block whose separator is displayed line `line`: stage it, or
    // unstage it when staged.
    void applyRequested(int line);
    void discardRequested(int line);

private:
    // Over the block under `y`, in the viewport; hidden outside a block.
    void placeAt(int y);
    void hide();

    DiffView *diff_;
    QFrame *bar_; // holds the buttons
    QToolButton *apply_;
    QToolButton *discard_;
    QIcon stageIcon_;
    QIcon unstageIcon_;
    bool staged_ = false;
    int hunkLine_ = -1; // the separator of the block they act on
};
