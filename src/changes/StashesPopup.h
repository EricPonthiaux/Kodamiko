#pragma once

#include "git/Stashes.h"
#include "ui/OverlayPopup.h"
#include "ui/Theme.h"

class DiffView;
class GitActions;
class PopupHeader;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
struct DiffFile;

// Git ▸ Stashes, a popup over the window: the changes put aside with
// "git stash", newest first, and the diff of the selected one. Pop brings
// it back and drops it, Apply keeps it.
class StashesPopup : public OverlayPopup {
    Q_OBJECT

public:
    StashesPopup(GitActions *actions, const Theme &theme, QWidget *parent);

    // Lists the stashes of the repository holding `folder` and shows the
    // popup.
    void popup(const QString &folder);
    // popup() without showing, for tests.
    void load(const QString &folder);
    void applyTheme(const Theme &theme);

    QListWidget *list() const { return list_; }

    DiffView *diff() const { return diff_; }

    // A stash's patch, one DiffFile per file.
    static QList<DiffFile> splitPatch(const QByteArray &patch);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void showSelected();
    // Runs `action` on the selected stash, closing the popup.
    void act(const std::function<void(const Stash &)> &action);

    Theme theme_;
    GitActions *actions_;
    QString workDir_;
    QList<Stash> stashes_;

    PopupHeader *header_;
    QStackedWidget *body_;
    QListWidget *list_;
    DiffView *diff_;
    QLabel *emptyText_;
    QPushButton *drop_;
    QPushButton *apply_;
    QPushButton *pop_;
};
