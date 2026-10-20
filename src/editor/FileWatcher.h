// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QSet>
#include <QTimer>

// Watches open files and reports their modifications on disk.
//
// Bursts of notifications (a tool writing a file in several steps) are
// merged into a single signal per file.
class FileWatcher : public QObject {
    Q_OBJECT

public:
    explicit FileWatcher(QObject *parent = nullptr);

    void watch(const QString &path);
    void unwatch(const QString &path);

signals:
    void fileChanged(const QString &path);

private:
    void onFileChanged(const QString &path);
    void emitPendingChanges();

    QFileSystemWatcher watcher_;
    QSet<QString> watchedPaths_;
    QSet<QString> pendingPaths_;
    QTimer debounce_;
};
