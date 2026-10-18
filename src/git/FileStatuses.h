#pragma once

#include <QHash>
#include <QObject>
#include <QThreadPool>

#include <optional>

// State of a working directory file as the file tree shows it.
enum class FileStatus {
    Modified,
    Added,
    Untracked,
    Renamed,
    Conflicted,
    Ignored
};

// States of the files of a repository, by absolute path. Unchanged files are
// absent; deleted files too (the tree cannot show them). An ignored folder
// appears once, without its contents.
using FileStatusMap = QHash<QString, FileStatus>;

namespace FileStatuses {

// Reads the states for the repository containing `folder`: empty outside a
// repository. Usable off the UI thread. Throws GitError.
FileStatusMap read(const QString &folder);

// State of the folders under `root` that contain changes (IntelliJ's "Show
// directories with changed descendants"): the worst state of their files,
// conflicted, then modified, renamed, added, untracked. Ignored files do
// not count.
FileStatusMap folderStatuses(const FileStatusMap &statuses, const QString &root);

} // namespace FileStatuses

// Reads the file states off the UI thread. Requests made while a read is in
// progress are merged into a single new read.
class FileStatusReader : public QObject {
    Q_OBJECT

public:
    explicit FileStatusReader(QObject *parent = nullptr);
    ~FileStatusReader() override;

    void setFolder(const QString &folder);
    void refresh();

    const FileStatusMap &statuses() const { return statuses_; }

signals:
    void statusesChanged();

private:
    QString folder_;
    FileStatusMap statuses_;
    QThreadPool pool_;
    bool running_ = false;
    bool pending_ = false;
    int generation_ = 0;
};
