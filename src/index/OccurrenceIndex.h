// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "OccurrenceStore.h"

#include <QObject>
#include <QThreadPool>

#include <atomic>
#include <memory>

// Every word of the working folder's files and where it occurs (see
// OccurrenceStore), kept up to date off the UI thread: the files are
// parsed in parallel, on all the cores but one, and only those modified
// since the previous indexing are read again. The index is saved in the
// cache folder once indexed, and loaded back when the folder is opened
// again, so that a restart reads only what changed meanwhile. Files
// ignored by git, binary files and files of more than 2 MB are left out.
class OccurrenceIndex : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(OccurrenceIndex)

public:
    explicit OccurrenceIndex(QObject *parent = nullptr);
    // Stops the indexing in progress and waits for it.
    ~OccurrenceIndex() override;

    // Indexes `folder`, starting from its saved index if any.
    void setFolder(const QString &folder);

    QString folder() const { return folder_; }

    // Reads again the files added, modified or removed since.
    void refresh();
    // Indexes `text` as the content of `path` (unsaved changes), in the
    // background; the next refresh() reads the saved file again.
    void updateFile(const QString &path, const QByteArray &text);

    bool isIndexing() const { return running_; }

    // What is indexed so far; thread-safe.
    const OccurrenceStore &store() const { return *store_; }

    // Where the index of `folder` is saved.
    static QString cachePathFor(const QString &folder);

signals:
    // An indexing finished (not one abandoned for another).
    void indexingFinished();

private:
    struct Task;
    // Runs on the coordinator's thread.
    void index(const Task &task);
    void finish(int generation);

    QString folder_;
    // Shared with the tasks, which may outlive a folder change.
    std::shared_ptr<OccurrenceStore> store_ = std::make_shared<OccurrenceStore>();
    QThreadPool coordinator_; // one indexing at a time
    QThreadPool parsers_;
    std::shared_ptr<std::atomic_int> generation_ = std::make_shared<std::atomic_int>(0);
    bool running_ = false;
    bool refreshPending_ = false;
    bool loadPending_ = false; // the next indexing loads the saved index first
};
