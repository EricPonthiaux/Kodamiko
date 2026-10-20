// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

// Reports changes to the git folder: commit, branch switch, staging… done
// from a terminal, by Claude or by the editor. Changes to the project's files
// are not tracked here.
//
// git replaces its files (index, HEAD, refs) by renaming: so it is the
// folders that are watched, and bursts are coalesced into one signal.
class RepositoryWatcher : public QObject {
    Q_OBJECT

public:
    explicit RepositoryWatcher(QObject *parent = nullptr);

    // Git folder to watch; empty to stop watching.
    void setGitDir(const QString &gitDir);

    QString gitDir() const { return gitDir_; }

signals:
    void changed();

private:
    QFileSystemWatcher watcher_;
    QTimer debounce_;
    QString gitDir_;
};
