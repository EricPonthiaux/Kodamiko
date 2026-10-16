#pragma once

#include "git/Snapshots.h"

#include <QDialog>

class GitCommands;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

// List of snapshots taken before discarding changes, and restoring one of
// them (itself preceded by a snapshot).
class SnapshotsDialog : public QDialog {
    Q_OBJECT

public:
    SnapshotsDialog(const QString &folder, GitCommands *commands, QWidget *parent = nullptr);

private:
    void showDetails();
    void restoreSelected();

    QString folder_;
    GitCommands *commands_;
    QList<Snapshot> snapshots_;
    QListWidget *list_;
    QPlainTextEdit *details_;
    QPushButton *restore_;
};
