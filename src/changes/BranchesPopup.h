#pragma once

#include "git/Branches.h"
#include "ui/OverlayPopup.h"
#include "ui/Theme.h"

class GitActions;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

// Git ▸ Branches, a popup over the window like Find in Files: the local
// branches, then the remote ones, filtered as you type, each with how far
// it is from its upstream. Enter switches to the selected branch; the
// buttons and the context menu merge, rebase, rename, delete or track it.
class BranchesPopup : public OverlayPopup {
    Q_OBJECT

public:
    BranchesPopup(GitActions *actions, const Theme &theme, QWidget *parent);

    // Lists the branches of the repository holding `folder` and shows the
    // popup.
    void popup(const QString &folder);
    // popup() without showing, for tests.
    void load(const QString &folder);
    void applyTheme(const Theme &theme);

    QLineEdit *filterField() const { return filter_; }

    QListWidget *list() const { return list_; }

    QPushButton *switchButton() const { return switch_; }

    // The selected branch, if any.
    const BranchInfo *selected() const;
    void switchToSelected();

signals:
    void newBranchRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void rebuildList();
    void updateButtons();
    void showMenu(const QPoint &position);
    // Runs `action` on the selected branch, closing the popup.
    void act(const std::function<void(const BranchInfo &)> &action);

    Theme theme_;
    GitActions *actions_;
    QString folder_;
    QString current_; // the current branch, empty when detached
    QList<BranchInfo> branches_;

    QLabel *headerIcon_;
    QLabel *subtitle_;
    QLineEdit *filter_;
    QListWidget *list_;
    QLabel *empty_;
    QPushButton *merge_;
    QPushButton *rebase_;
    QPushButton *switch_;
};
