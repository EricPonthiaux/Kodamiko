// SPDX-License-Identifier: GPL-3.0-only

#include "CommitLog.h"

#include "ContentMatcher.h"
#include "LibGit2.h"

#include <QHash>
#include <QObject>
#include <QTimeZone>

#include <array>

using libgit2::check;
using libgit2::Ptr;

namespace {

// What a log's walk keeps, and where it is.
struct LogWalk {
    git_repository *repository = nullptr;
    QString prefix; // the open folder in the repository
    bool followsPath = false;
    // Repository path whose commits are kept (empty: all of them); for a
    // followed file, its name in the commits walked next.
    QString filter;
    std::optional<ContentMatcher> contents;
};

QString idOf(const git_oid *oid)
{
    std::array<char, GIT_OID_MAX_HEXSIZE + 1> text{};
    git_oid_tostr(text.data(), text.size(), oid);
    return QString::fromLatin1(text.data());
}

QDateTime dateOf(const git_time &time)
{
    return QDateTime::fromSecsSinceEpoch(time.time, QTimeZone::fromSecondsAheadOfUtc(time.offset * 60));
}

// Opens the repository; throws GitError if there is none.
Ptr<git_repository> requireRepository(const QString &folder)
{
    Ptr<git_repository> repository = libgit2::openRepository(folder);
    if (!repository)
        throw GitError(QObject::tr("This folder is not in a git repository."));
    return repository;
}

// Short reference names, by target commit. HEAD first in its list.
QHash<QString, QStringList> refsByCommit(git_repository *repository)
{
    QHash<QString, QStringList> result;
    git_reference_iterator *iterator = nullptr;
    if (git_reference_iterator_new(&iterator, repository) < 0)
        return result;
    const Ptr<git_reference_iterator> iteratorGuard(iterator);
    git_reference *reference = nullptr;
    while (git_reference_next(&reference, iterator) == 0) {
        const Ptr<git_reference> referenceGuard(reference);
        if (!git_reference_is_branch(reference) && !git_reference_is_remote(reference) &&
            !git_reference_is_tag(reference))
            continue;
        git_object *target = nullptr;
        if (git_reference_peel(&target, reference, GIT_OBJECT_COMMIT) < 0)
            continue;
        const Ptr<git_object> targetGuard(target);
        const QString name = QString::fromUtf8(git_reference_shorthand(reference));
        if (name.endsWith(QLatin1String("/HEAD")))
            continue; // "origin/HEAD" duplicates the remote branch it points to
        result[idOf(git_object_id(target))].append(name);
    }
    for (QStringList &names : result)
        names.sort();
    if (git_repository_head_detached(repository) == 1) {
        git_oid head;
        if (git_reference_name_to_id(&head, repository, "HEAD") == 0)
            result[idOf(&head)].prepend(QStringLiteral("HEAD"));
    }
    return result;
}

// Not const: git_commit_summary caches the summary inside the commit.
CommitSummary summaryOf(git_commit *commit, const QHash<QString, QStringList> &refs)
{
    CommitSummary summary;
    summary.id = idOf(git_commit_id(commit));
    summary.shortId = summary.id.left(7);
    summary.summary = QString::fromUtf8(git_commit_summary(commit));
    summary.message = QString::fromUtf8(git_commit_message(commit)).trimmed();
    const git_signature *author = git_commit_author(commit);
    summary.author = QString::fromUtf8(author->name);
    summary.email = QString::fromUtf8(author->email);
    summary.date = dateOf(author->when);
    for (unsigned int i = 0; i < git_commit_parentcount(commit); ++i)
        summary.parentIds.append(idOf(git_commit_parent_id(commit, i)));
    summary.refs = refs.value(summary.id);
    return summary;
}

// Identifier of the file or folder `path` ("src/main.cpp", "src") in the
// commit; zero if it does not exist there.
git_oid entryId(const git_commit *commit, const QString &path)
{
    git_oid result{};
    git_tree *tree = nullptr;
    if (git_commit_tree(&tree, commit) < 0)
        return result;
    const Ptr<git_tree> treeGuard(tree);
    git_tree_entry *entry = nullptr;
    if (git_tree_entry_bypath(&entry, tree, path.toUtf8().constData()) < 0)
        return result;
    const Ptr<git_tree_entry> entryGuard(entry);
    git_oid_cpy(&result, git_tree_entry_id(entry));
    return result;
}

// True if the commit modifies `path` relative to its first parent.
bool touchesPath(const git_commit *commit, const QString &path)
{
    const git_oid own = entryId(commit, path);
    if (git_commit_parentcount(commit) == 0)
        return !git_oid_is_zero(&own);
    git_commit *parent = nullptr;
    check(git_commit_parent(&parent, commit, 0), "Reading a parent commit");
    const Ptr<git_commit> parentGuard(parent);
    const git_oid before = entryId(parent, path);
    return !git_oid_equal(&own, &before);
}

// The path `commit` renamed into `path`; empty if it created `path` or
// only changed it.
QString renamedFrom(git_repository *repository, const git_commit *commit, const QString &path)
{
    if (git_commit_parentcount(commit) == 0)
        return {};
    git_commit *parent = nullptr;
    check(git_commit_parent(&parent, commit, 0), "Reading a parent commit");
    const Ptr<git_commit> parentGuard(parent);
    // Only a commit that creates `path` can have renamed it: the others need
    // no diff.
    if (const git_oid before = entryId(parent, path); !git_oid_is_zero(&before))
        return {};
    git_tree *tree = nullptr;
    check(git_commit_tree(&tree, commit), "Reading a commit");
    const Ptr<git_tree> treeGuard(tree);
    git_tree *parentTree = nullptr;
    check(git_commit_tree(&parentTree, parent), "Reading a parent commit");
    const Ptr<git_tree> parentTreeGuard(parentTree);

    git_diff *diff = nullptr;
    check(git_diff_tree_to_tree(&diff, repository, parentTree, tree, nullptr), "Following a rename");
    const Ptr<git_diff> diffGuard(diff);
    git_diff_find_options findOptions;
    git_diff_find_options_init(&findOptions, GIT_DIFF_FIND_OPTIONS_VERSION);
    findOptions.flags = GIT_DIFF_FIND_RENAMES;
    check(git_diff_find_similar(diff, &findOptions), "Following a rename");
    const QByteArray target = path.toUtf8();
    for (size_t i = 0; i < git_diff_num_deltas(diff); ++i) {
        const git_diff_delta *delta = git_diff_get_delta(diff, i);
        if (delta->status == GIT_DELTA_RENAMED && target == delta->new_file.path)
            return QString::fromUtf8(delta->old_file.path);
    }
    return {};
}

// The path to look for in the commits before `commit`: `path`, or the one it
// had before `commit` renamed it from within the open folder (`prefix`).
QString pathBefore(git_repository *repository, const git_commit *commit, const QString &path, const QString &prefix)
{
    const QString renamed = renamedFrom(repository, commit, path);
    return renamed.isEmpty() || !renamed.startsWith(prefix) ? path : renamed;
}

// A walk from HEAD, or from every branch, newest first and a commit before
// its parents.
Ptr<git_revwalk> startWalk(git_repository *repository, LogScope scope)
{
    git_revwalk *walk = nullptr;
    check(git_revwalk_new(&walk, repository), "Walking the history");
    Ptr<git_revwalk> result(walk);
    check(git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME), "Walking the history");
    check(git_revwalk_push_head(walk), "Walking the history");
    if (scope == LogScope::AllBranches) {
        check(git_revwalk_push_glob(walk, "refs/heads"), "Walking the branches");
        check(git_revwalk_push_glob(walk, "refs/remotes"), "Walking the branches");
    }
    return result;
}

// The repository path `commit` is kept for (the filter; empty without one),
// or nullopt if the walk leaves it out. A followed file is then looked for
// under its name before the commit.
std::optional<QString> keptPath(LogWalk &walk, const git_commit *commit)
{
    if (!walk.filter.isEmpty() && !touchesPath(commit, walk.filter))
        return std::nullopt;
    QString path = walk.filter;
    // Followed even across the commits the content search leaves out.
    if (walk.followsPath)
        walk.filter = pathBefore(walk.repository, commit, walk.filter, walk.prefix);
    if (walk.contents && !walk.contents->matches(walk.repository, commit, path))
        return std::nullopt;
    return path;
}

} // namespace

std::optional<CommitLog> CommitLog::read(const QString &folder, const LogOptions &options)
{
    const std::optional<RepositoryInfo> info = RepositoryInfo::read(folder);
    if (!info)
        return std::nullopt;

    CommitLog log;
    log.info = *info;
    if (info->unborn)
        return log;

    const libgit2::Session session;
    const Ptr<git_repository> repository = requireRepository(folder);
    const QHash<QString, QStringList> refs = refsByCommit(repository.get());
    const Ptr<git_revwalk> revwalk = startWalk(repository.get(), options.scope);
    LogWalk walk{
        .repository = repository.get(),
        .prefix = info->prefix,
        .followsPath = !options.path.isEmpty(),
        .filter = options.path.isEmpty() ? info->prefix : info->repoPath(options.path),
        .contents = std::nullopt,
    };
    if (walk.filter.endsWith(QLatin1Char('/')))
        walk.filter.chop(1);
    if (options.contents)
        walk.contents.emplace(*options.contents);

    git_oid oid;
    while (!options.stop.stop_requested() && git_revwalk_next(&oid, revwalk.get()) == 0) {
        git_commit *commit = nullptr;
        check(git_commit_lookup(&commit, repository.get(), &oid), "Reading a commit");
        const Ptr<git_commit> commitGuard(commit);
        const std::optional<QString> path = keptPath(walk, commit);
        if (!path)
            continue;
        if (log.commits.size() == options.limit) {
            log.truncated = true;
            break;
        }
        log.commits.append(summaryOf(commit, refs));
        if (walk.followsPath)
            log.commits.last().path = path->mid(info->prefix.size());
    }
    return log;
}

CommitDetails CommitDetails::read(const QString &folder, const QString &commitId, Whitespace whitespace)
{
    const std::optional<RepositoryInfo> info = RepositoryInfo::read(folder);
    if (!info)
        throw GitError(QObject::tr("This folder is not in a git repository."));

    const libgit2::Session session;
    const Ptr<git_repository> repository = requireRepository(folder);

    git_oid oid;
    check(git_oid_fromstr(&oid, commitId.toLatin1().constData()), "Reading the commit");
    git_commit *commit = nullptr;
    check(git_commit_lookup(&commit, repository.get(), &oid), "Reading the commit");
    const Ptr<git_commit> commitGuard(commit);

    CommitDetails details;
    details.summary = summaryOf(commit, refsByCommit(repository.get()));
    details.message = QString::fromUtf8(git_commit_message(commit)).trimmed();
    const git_signature *author = git_commit_author(commit);
    const git_signature *committer = git_commit_committer(commit);
    if (qstrcmp(author->name, committer->name) != 0 || qstrcmp(author->email, committer->email) != 0)
        details.committer =
            QStringLiteral("%1 <%2>").arg(QString::fromUtf8(committer->name), QString::fromUtf8(committer->email));
    details.commitDate = dateOf(committer->when);
    for (unsigned int i = 0; i < git_commit_parentcount(commit); ++i)
        details.parentIds.append(idOf(git_commit_parent_id(commit, i)).left(7));

    git_tree *tree = nullptr;
    check(git_commit_tree(&tree, commit), "Reading the commit");
    const Ptr<git_tree> treeGuard(tree);
    Ptr<git_tree> parentTree;
    if (git_commit_parentcount(commit) > 0) {
        git_commit *parent = nullptr;
        check(git_commit_parent(&parent, commit, 0), "Reading a parent commit");
        const Ptr<git_commit> parentGuard(parent);
        git_tree *raw = nullptr;
        check(git_commit_tree(&raw, parent), "Reading a parent commit");
        parentTree.reset(raw);
    }

    QByteArray prefixBytes = info->prefix.toUtf8();
    char *pathspec = prefixBytes.data();
    git_diff_options options;
    git_diff_options_init(&options, GIT_DIFF_OPTIONS_VERSION);
    if (!info->prefix.isEmpty()) {
        options.pathspec.strings = &pathspec;
        options.pathspec.count = 1;
    }
    if (whitespace == Whitespace::Ignore)
        options.flags |= GIT_DIFF_IGNORE_WHITESPACE;
    git_diff *diff = nullptr;
    check(git_diff_tree_to_tree(&diff, repository.get(), parentTree.get(), tree, &options), "Computing the diff");
    const Ptr<git_diff> diffGuard(diff);
    git_diff_find_options findOptions;
    git_diff_find_options_init(&findOptions, GIT_DIFF_FIND_OPTIONS_VERSION);
    findOptions.flags = GIT_DIFF_FIND_RENAMES;
    check(git_diff_find_similar(diff, &findOptions), "Detecting renames");
    details.files = libgit2::fileDiffs(diff, info->prefix);
    return details;
}
