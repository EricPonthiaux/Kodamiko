#include "FileStatuses.h"

#include "LibGit2.h"

#include <QDir>
#include <QFileInfo>

using libgit2::check;
using libgit2::Ptr;

namespace {

std::optional<FileStatus> statusOf(unsigned int flags)
{
    if (flags & GIT_STATUS_CONFLICTED)
        return FileStatus::Conflicted;
    if (flags & GIT_STATUS_IGNORED)
        return FileStatus::Ignored;
    if (flags & (GIT_STATUS_INDEX_RENAMED | GIT_STATUS_WT_RENAMED))
        return FileStatus::Renamed;
    if (flags & GIT_STATUS_INDEX_NEW)
        return FileStatus::Added;
    if (flags & GIT_STATUS_WT_NEW)
        return FileStatus::Untracked;
    if (flags & (GIT_STATUS_INDEX_MODIFIED | GIT_STATUS_WT_MODIFIED | GIT_STATUS_INDEX_TYPECHANGE | GIT_STATUS_WT_TYPECHANGE))
        return FileStatus::Modified;
    return std::nullopt; // deleted, or unchanged
}

int severity(FileStatus status)
{
    switch (status) {
    case FileStatus::Conflicted: return 5;
    case FileStatus::Modified: return 4;
    case FileStatus::Renamed: return 3;
    case FileStatus::Added: return 2;
    case FileStatus::Untracked: return 1;
    case FileStatus::Ignored: return 0;
    }
    return 0;
}

} // namespace

namespace FileStatuses {

FileStatusMap read(const QString &folder)
{
    FileStatusMap result;
    const libgit2::Session session;
    const Ptr<git_repository> repository = libgit2::openRepository(folder);
    if (!repository)
        return result;
    QString workDir = QString::fromUtf8(git_repository_workdir(repository.get()));
    if (workDir.endsWith(QLatin1Char('/')))
        workDir.chop(1);
    // Paths as the tree sees them: the open folder may go through a symbolic link.
    const QString canonicalWorkDir = QFileInfo(workDir).canonicalFilePath();
    const QString canonicalFolder = QFileInfo(folder).canonicalFilePath();
    const QString folderPath = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    const QString prefix = QDir(canonicalWorkDir).relativeFilePath(canonicalFolder);

    git_status_options options;
    git_status_options_init(&options, GIT_STATUS_OPTIONS_VERSION);
    options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS
                    | GIT_STATUS_OPT_INCLUDE_IGNORED | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX
                    | GIT_STATUS_OPT_EXCLUDE_SUBMODULES;
    QByteArray pathspec;
    char *pathspecs[1];
    if (prefix != QLatin1String(".") && !prefix.startsWith(QLatin1String(".."))) {
        pathspec = prefix.toUtf8() + '/';
        pathspecs[0] = pathspec.data();
        options.pathspec = {pathspecs, 1};
        options.flags |= GIT_STATUS_OPT_DISABLE_PATHSPEC_MATCH;
    }

    git_status_list *list = nullptr;
    check(git_status_list_new(&list, repository.get(), &options), "Reading the file states");
    const std::unique_ptr<git_status_list, decltype(&git_status_list_free)> listGuard(list, git_status_list_free);
    const size_t count = git_status_list_entrycount(list);
    for (size_t i = 0; i < count; ++i) {
        const git_status_entry *entry = git_status_byindex(list, i);
        const std::optional<FileStatus> status = statusOf(entry->status);
        if (!status)
            continue;
        const git_diff_delta *delta = entry->index_to_workdir ? entry->index_to_workdir : entry->head_to_index;
        if (!delta || !delta->new_file.path)
            continue;
        QString path = QString::fromUtf8(delta->new_file.path);
        if (path.endsWith(QLatin1Char('/')))
            path.chop(1); // ignored folder
        // Relative to the open folder, then absolute as the tree shows it.
        if (!pathspec.isEmpty())
            path = path.mid(pathspec.size());
        result.insert(folderPath + QLatin1Char('/') + path, *status);
    }
    return result;
}

FileStatusMap folderStatuses(const FileStatusMap &statuses, const QString &root)
{
    FileStatusMap folders;
    for (auto it = statuses.cbegin(); it != statuses.cend(); ++it) {
        if (it.value() == FileStatus::Ignored)
            continue;
        for (QString folder = QFileInfo(it.key()).absolutePath(); folder.size() >= root.size();
             folder = QFileInfo(folder).absolutePath()) {
            const auto existing = folders.constFind(folder);
            if (existing != folders.cend() && severity(*existing) >= severity(it.value()))
                break; // the folders above already have at least this state
            folders.insert(folder, it.value());
            if (folder == root)
                break;
        }
    }
    return folders;
}

} // namespace FileStatuses

FileStatusReader::FileStatusReader(QObject *parent)
    : QObject(parent)
{
    pool_.setMaxThreadCount(1);
}

FileStatusReader::~FileStatusReader()
{
    pool_.waitForDone();
}

void FileStatusReader::setFolder(const QString &folder)
{
    folder_ = folder;
    ++generation_;
    statuses_.clear();
    emit statusesChanged();
    refresh();
}

void FileStatusReader::refresh()
{
    if (folder_.isEmpty())
        return;
    if (running_) {
        pending_ = true;
        return;
    }
    running_ = true;
    pool_.start([this, folder = folder_, generation = generation_] {
        FileStatusMap statuses;
        try {
            statuses = FileStatuses::read(folder);
        } catch (const GitError &) {
            // Unreadable repository: no colors.
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, statuses = std::move(statuses)] {
                running_ = false;
                if (generation == generation_ && statuses != statuses_) {
                    statuses_ = statuses;
                    emit statusesChanged();
                }
                if (std::exchange(pending_, false) || generation != generation_)
                    refresh();
            },
            Qt::QueuedConnection);
    });
}
