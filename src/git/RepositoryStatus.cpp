#include "RepositoryStatus.h"

#include "LibGit2.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>

using libgit2::check;
using libgit2::fileDiffs;
using libgit2::openRepository;
using libgit2::Ptr;

namespace libgit2 {

Ptr<git_repository> openRepository(const QString &folder)
{
    git_repository *repository = nullptr;
    const QByteArray folderPath = QDir::toNativeSeparators(folder).toUtf8();
    const int opened = git_repository_open_ext(&repository, folderPath.constData(), 0, nullptr);
    if (opened == GIT_ENOTFOUND)
        return nullptr;
    check(opened, "Opening the git repository");
    Ptr<git_repository> guard(repository);
    if (git_repository_is_bare(repository))
        return nullptr;
    return guard;
}

} // namespace libgit2

namespace {

QString withoutTrailingSlash(const char *path)
{
    QString result = QString::fromUtf8(path);
    if (result.endsWith(QLatin1Char('/')))
        result.chop(1);
    return result;
}

QString operationName(int state)
{
    switch (state) {
    case GIT_REPOSITORY_STATE_MERGE:
        return QObject::tr("merge");
    case GIT_REPOSITORY_STATE_REVERT:
    case GIT_REPOSITORY_STATE_REVERT_SEQUENCE:
        return QObject::tr("revert");
    case GIT_REPOSITORY_STATE_CHERRYPICK:
    case GIT_REPOSITORY_STATE_CHERRYPICK_SEQUENCE:
        return QObject::tr("cherry-pick");
    case GIT_REPOSITORY_STATE_BISECT:
        return QObject::tr("bisect");
    case GIT_REPOSITORY_STATE_REBASE:
    case GIT_REPOSITORY_STATE_REBASE_INTERACTIVE:
    case GIT_REPOSITORY_STATE_REBASE_MERGE:
        return QObject::tr("rebase");
    case GIT_REPOSITORY_STATE_APPLY_MAILBOX:
    case GIT_REPOSITORY_STATE_APPLY_MAILBOX_OR_REBASE:
        return QObject::tr("git am");
    default:
        return {};
    }
}

// The branch `head` tracks, and how far apart they are. A branch without
// upstream, or whose upstream is gone (deleted on the remote), has none.
void readUpstream(git_repository *repository, git_reference *head, RepositoryInfo &info)
{
    git_reference *upstream = nullptr;
    if (git_branch_upstream(&upstream, head) != 0)
        return;
    const Ptr<git_reference> upstreamGuard(upstream);
    const git_oid *local = git_reference_target(head);
    const git_oid *remote = git_reference_target(upstream);
    if (!local || !remote)
        return;
    size_t ahead = 0;
    size_t behind = 0;
    check(git_graph_ahead_behind(&ahead, &behind, repository, local, remote), "Comparing with the upstream branch");
    info.upstream = QString::fromUtf8(git_reference_shorthand(upstream));
    info.ahead = static_cast<int>(ahead);
    info.behind = static_cast<int>(behind);
}

RepositoryInfo readInfo(git_repository *repository, const QString &folder)
{
    RepositoryInfo info;
    info.gitDir = withoutTrailingSlash(git_repository_path(repository));
    const QString workDir = withoutTrailingSlash(git_repository_workdir(repository));
    info.workDir = QFileInfo(workDir).canonicalFilePath();
    const QString prefix = QDir(info.workDir).relativeFilePath(QFileInfo(folder).canonicalFilePath());
    info.prefix = prefix == QLatin1String(".") ? QString() : prefix + QLatin1Char('/');
    info.operation = operationName(git_repository_state(repository));

    info.unborn = git_repository_head_unborn(repository) == 1;
    if (info.unborn) {
        // HEAD points to a branch that has no commits yet.
        git_reference *head = nullptr;
        check(git_reference_lookup(&head, repository, "HEAD"), "Reading HEAD");
        const Ptr<git_reference> headGuard(head);
        info.branch = QString::fromUtf8(git_reference_symbolic_target(head)).remove(QStringLiteral("refs/heads/"));
        return info;
    }

    git_reference *head = nullptr;
    check(git_repository_head(&head, repository), "Reading HEAD");
    const Ptr<git_reference> headGuard(head);
    info.detached = git_repository_head_detached(repository) == 1;
    if (!info.detached) {
        info.branch = QString::fromUtf8(git_reference_shorthand(head));
        readUpstream(repository, head, info);
    }

    git_object *commit = nullptr;
    check(git_reference_peel(&commit, head, GIT_OBJECT_COMMIT), "Reading the last commit");
    const Ptr<git_object> commitGuard(commit);
    git_buf shortId = GIT_BUF_INIT;
    check(git_object_short_id(&shortId, commit), "Reading the last commit");
    info.headId = QString::fromUtf8(shortId.ptr, static_cast<qsizetype>(shortId.size));
    git_buf_dispose(&shortId);
    info.headMessage = QString::fromUtf8(git_commit_message(reinterpret_cast<git_commit *>(commit)));
    return info;
}

} // namespace

namespace libgit2 {

FileChange fileChangeOf(const git_diff_delta *delta, const QString &prefix)
{
    const QString newPath = QString::fromUtf8(delta->new_file.path).mid(prefix.size());
    const QString oldPath = QString::fromUtf8(delta->old_file.path).mid(prefix.size());
    switch (delta->status) {
    case GIT_DELTA_ADDED:
    case GIT_DELTA_UNTRACKED:
        return {ChangeKind::Added, newPath, {}};
    case GIT_DELTA_DELETED:
        return {ChangeKind::Deleted, oldPath, {}};
    case GIT_DELTA_RENAMED:
        return {ChangeKind::Renamed, newPath, oldPath};
    case GIT_DELTA_CONFLICTED:
        return {ChangeKind::Conflicted, newPath, {}};
    default:
        return {ChangeKind::Modified, newPath, {}};
    }
}

QList<FileDiff> fileDiffs(git_diff *diff, const QString &prefix)
{
    QList<FileDiff> result;
    const size_t deltaCount = git_diff_num_deltas(diff);
    for (size_t i = 0; i < deltaCount; ++i) {
        git_patch *patch = nullptr;
        check(git_patch_from_diff(&patch, diff, i), "Computing the diff");
        const Ptr<git_patch> patchGuard(patch);
        git_buf buffer = GIT_BUF_INIT;
        check(git_patch_to_buf(&buffer, patch), "Formatting the diff");
        const QByteArray text(buffer.ptr, static_cast<qsizetype>(buffer.size));
        git_buf_dispose(&buffer);
        result.append({fileChangeOf(git_diff_get_delta(diff, i), prefix), text});
    }
    return result;
}

} // namespace libgit2

std::optional<RepositoryInfo> RepositoryInfo::read(const QString &folder)
{
    const libgit2::Session session;
    const Ptr<git_repository> repository = openRepository(folder);
    if (!repository)
        return std::nullopt;
    return readInfo(repository.get(), folder);
}

std::optional<RepositoryStatus> RepositoryStatus::read(const QString &folder)
{
    const libgit2::Session session;
    const Ptr<git_repository> repository = openRepository(folder);
    if (!repository)
        return std::nullopt;

    RepositoryStatus status;
    status.info = readInfo(repository.get(), folder);

    // Repository without commits: the index is compared to an empty tree.
    Ptr<git_tree> headTree;
    if (!status.info.unborn) {
        git_object *tree = nullptr;
        check(git_revparse_single(&tree, repository.get(), "HEAD^{tree}"), "Reading the last commit");
        headTree.reset(reinterpret_cast<git_tree *>(tree));
    }

    git_index *index = nullptr;
    check(git_repository_index(&index, repository.get()), "Reading the index");
    const Ptr<git_index> indexGuard(index);

    QByteArray prefixBytes = status.info.prefix.toUtf8();
    char *pathspec[] = {prefixBytes.data()};
    git_diff_options options;
    git_diff_options_init(&options, GIT_DIFF_OPTIONS_VERSION);
    if (!status.info.prefix.isEmpty()) {
        options.pathspec.strings = pathspec;
        options.pathspec.count = 1;
    }

    git_diff *staged = nullptr;
    check(git_diff_tree_to_index(&staged, repository.get(), headTree.get(), index, &options),
          "Comparing the index with the last commit");
    const Ptr<git_diff> stagedGuard(staged);
    git_diff_find_options findOptions;
    git_diff_find_options_init(&findOptions, GIT_DIFF_FIND_OPTIONS_VERSION);
    findOptions.flags = GIT_DIFF_FIND_RENAMES;
    check(git_diff_find_similar(staged, &findOptions), "Detecting renames");
    status.staged = fileDiffs(staged, status.info.prefix);

    // Like "git status": no rename detection on the disk side, a moved file
    // shows up as deleted on one side and untracked on the other.
    options.flags = GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_RECURSE_UNTRACKED_DIRS | GIT_DIFF_SHOW_UNTRACKED_CONTENT;
    git_diff *unstaged = nullptr;
    check(git_diff_index_to_workdir(&unstaged, repository.get(), index, &options),
          "Comparing the files with the index");
    const Ptr<git_diff> unstagedGuard(unstaged);
    status.unstaged = fileDiffs(unstaged, status.info.prefix);
    return status;
}
