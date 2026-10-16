#pragma once

#include "git/Snapshots.h"
#include "syntax/Theme.h"
#include "workspace/OverlayPopup.h"

class GitCommands;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;

// Git ▸ Restore a Snapshot, a popup over the window like Find in Files: the
// snapshots taken before discarding changes and before each Claude prompt,
// newest first, and the files of the selected one. Restoring takes a
// snapshot of the current files first, so it can be undone.
class SnapshotsPopup : public OverlayPopup {
    Q_OBJECT

public:
    SnapshotsPopup(GitCommands *commands, const Theme &theme, QWidget *parent);

    // Lists the snapshots of the repository holding `folder` and shows the
    // popup.
    void popup(const QString &folder);
    // popup() without showing, for tests.
    void load(const QString &folder);
    void applyTheme(const Theme &theme);

    QListWidget *list() const { return list_; }
    QLabel *detailsTitle() const { return title_; }
    QListWidget *files() const { return files_; }
    void restoreSelected();

    // "Just now", "12 min ago", "Today at 14:32", "Yesterday at 9:05",
    // "Monday at 18:20", then the date.
    static QString describeDate(const QDateTime &date, const QDateTime &now);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    QWidget *createDetails();
    QWidget *createEmptyState();
    void showDetails();

    Theme theme_;
    GitCommands *commands_;
    QString folder_;
    QList<Snapshot> snapshots_;

    QStackedWidget *body_;
    QLabel *headerIcon_;
    QListWidget *list_;
    QLabel *title_ = nullptr;
    QLabel *date_ = nullptr;
    QLabel *badge_ = nullptr;
    QLabel *explanation_ = nullptr;
    QLabel *filesHeading_ = nullptr;
    QListWidget *files_ = nullptr;
    QLabel *commit_ = nullptr;
    QLabel *emptyIcon_ = nullptr;
    QLabel *emptyTitle_ = nullptr;
    QLabel *emptyText_ = nullptr;
    QPushButton *restore_;
};
