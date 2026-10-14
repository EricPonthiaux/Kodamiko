#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QSet>
#include <QTimer>

// Surveille les fichiers ouverts et signale leurs modifications sur disque.
//
// Les rafales de notifications (un outil qui écrit un fichier en plusieurs
// fois) sont regroupées en un seul signal par fichier.
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
