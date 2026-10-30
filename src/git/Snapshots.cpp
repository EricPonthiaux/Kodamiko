// SPDX-License-Identifier: GPL-3.0-only

#include "Snapshots.h"

#include "GitOperations.h"
#include "GitProcess.h"
#include "LibGit2.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <array>

using libgit2::check;
using libgit2::Ptr;

namespace {

const QString refPrefix = QStringLiteral("refs/kodamiko/snapshots/");
// Where snapshots were kept when the editor was called Enlumine, then Duet.
const std::array legacyRefPrefixes{QStringLiteral("refs/enlumine/snapshots/"), QStringLiteral("refs/duet/snapshots/")};
// Trailer of the commit message marking a snapshot of the whole working
// directory.
const QString wholeTreeTrailer = QStringLiteral("Scope: working tree");

// Snapshots are not the user's commits: fixed identity, never signed.
const QHash<QString, QString> &snapshotIdentity()
{
    static const QHash<QString, QString> identity{
        {QStringLiteral("GIT_AUTHOR_NAME"), QStringLiteral("Kodamiko")},
        {QStringLiteral("GIT_AUTHOR_EMAIL"), QStringLiteral("kodamiko@localhost")},
        {QStringLiteral("GIT_COMMITTER_NAME"), QStringLiteral("Kodamiko")},
        {QStringLiteral("GIT_COMMITTER_EMAIL"), QStringLiteral("kodamiko@localhost")},
    };
    return identity;
}

QStringList refsNewestFirst(const RepositoryInfo &repository)
{
    return QString::fromUtf8(
               Git::run(repository.workDir, {QStringLiteral("for-each-ref"), QStringLiteral("--sort=-refname"),
                                             QStringLiteral("--format=%(refname)"), refPrefix}))
        .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

void prune(const RepositoryInfo &repository)
{
    const QStringList refs = refsNewestFirst(repository);
    for (qsizetype i = Snapshots::maximumCount; i < refs.size(); ++i)
        Git::run(repository.workDir, {QStringLiteral("update-ref"), QStringLiteral("-d"), refs[i]});
}

// Paths reported by "git status" (a rename counts as a deletion and an
// addition).
QStringList changedPaths(const RepositoryInfo &repository)
{
    const QStringList fields = Git::splitNul(
        Git::run(repository.workDir,
                 {QStringLiteral("--no-optional-locks"), QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
                  QStringLiteral("-z"), QStringLiteral("--untracked-files=all"), QStringLiteral("--no-renames")}));
    QStringList paths;
    for (const QString &field : fields) {
        if (field.size() > 3)
            paths.append(field.mid(3)); // "XY path"
    }
    return paths;
}

// Commit message: the reason, a blank line, one path per line, then for a
// snapshot of the whole working directory a blank line and the trailer.
QString messageFor(const QString &reason, const QStringList &paths, bool wholeWorkingTree)
{
    QString message = reason + QStringLiteral("\n\n") + paths.join(QLatin1Char('\n')) + QLatin1Char('\n');
    if (wholeWorkingTree)
        message += QLatin1Char('\n') + wholeTreeTrailer + QLatin1Char('\n');
    return message;
}

void parseMessage(const QString &message, Snapshot &snapshot)
{
    const QStringList lines = message.split(QLatin1Char('\n'));
    snapshot.reason = lines.value(0);
    qsizetype i = 2;
    for (; i < lines.size() && !lines[i].isEmpty(); ++i)
        snapshot.paths.append(lines[i]);
    for (; i < lines.size(); ++i) {
        if (lines[i] == wholeTreeTrailer)
            snapshot.wholeWorkingTree = true;
    }
}

// Adds `paths` to the index named by `environment`. "git add" rejects a path
// that is neither tracked nor present on disk: such paths are left out.
void addExisting(const RepositoryInfo &repository, const QStringList &paths, const QHash<QString, QString> &environment)
{
    if (paths.isEmpty())
        return;
    const QStringList trackedList = Git::splitNul(Git::run(
        repository.workDir, QStringList{QStringLiteral("ls-files"), QStringLiteral("-z"), QStringLiteral("--")} + paths,
        {}, environment));
    const QSet<QString> tracked(trackedList.begin(), trackedList.end());
    QStringList existing;
    for (const QString &path : paths) {
        if (tracked.contains(path) || QFileInfo::exists(QDir(repository.workDir).filePath(path)))
            existing.append(path);
    }
    if (existing.isEmpty())
        return;
    Git::run(repository.workDir,
             QStringList{QStringLiteral("add"), QStringLiteral("-A"), QStringLiteral("--")} + existing, {},
             environment);
}

// The files still in conflict outside the snapshot's paths, during a merge or
// a rebase, take their version of HEAD in the temporary index: git cannot
// write a tree with conflicted entries.
void settleOtherConflicts(const RepositoryInfo &repository, const QHash<QString, QString> &environment)
{
    const QStringList unmerged = Git::splitNul(Git::run(repository.workDir,
                                                        {QStringLiteral("diff"), QStringLiteral("--name-only"),
                                                         QStringLiteral("-z"), QStringLiteral("--diff-filter=U")},
                                                        {}, environment));
    if (unmerged.isEmpty())
        return;
    Git::run(repository.workDir,
             QStringList{QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("HEAD"), QStringLiteral("--")} +
                 unmerged,
             {}, environment);
}

// Whether the most recent snapshot has the same tree, paths and scope.
bool matchesLastSnapshot(const RepositoryInfo &repository,
                         const QString &tree,
                         const QStringList &paths,
                         bool wholeWorkingTree)
{
    const QStringList refs = refsNewestFirst(repository);
    if (refs.isEmpty())
        return false;
    const QString previous =
        QString::fromUtf8(Git::run(repository.workDir, {QStringLiteral("log"), QStringLiteral("-1"),
                                                        QStringLiteral("--format=%T%n%B"), refs.first()}));
    const QString previousTree = previous.section(QLatin1Char('\n'), 0, 0);
    Snapshot last;
    parseMessage(previous.section(QLatin1Char('\n'), 1), last);
    return previousTree == tree && last.paths == paths && last.wholeWorkingTree == wholeWorkingTree;
}

// A reference name not used yet, sortable by date; the number prevents two
// snapshots with the same name.
QString unusedRefName(const RepositoryInfo &repository)
{
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    QString ref = refPrefix + stamp;
    for (int i = 1; !Git::run(repository.workDir, {QStringLiteral("for-each-ref"), ref}).isEmpty(); ++i)
        ref = refPrefix + stamp + QLatin1Char('-') + QString::number(i);
    return ref;
}

struct SnapshotRequest {
    QString reason;
    QStringList paths;
    // The other files are recorded too: `paths` must then list every changed file.
    bool wholeWorkingTree = false;
    // Nothing is recorded if the most recent snapshot has the same paths, the
    // same scope and the same content.
    bool skipIfUnchanged = false;
};

// Copies the real index to `copy`, keeping its modification time. Git reads
// the content of a file whose stat data matches its index entry only when
// the index is not older than the file ("racy git": a change of the same size
// in the second the index was written). A copy dated now would make git trust
// such an entry, and record the file as it was.
void copyIndex(const QString &realIndex, const QString &copy)
{
    if (!QFile::copy(realIndex, copy))
        throw GitError(QObject::tr("Cannot copy the index for the snapshot"));
    QFile file(copy);
    const bool isDated = file.open(QIODevice::ReadWrite) &&
                         file.setFileTime(QFileInfo(realIndex).lastModified(), QFileDevice::FileModificationTime);
    if (!isDated)
        throw GitError(QObject::tr("Cannot date the copy of the index for the snapshot: %1").arg(file.errorString()));
}

// Snapshot described by `request`; its reference name, or an empty string
// when skipped as unchanged.
QString createSnapshot(const RepositoryInfo &repository, const SnapshotRequest &request)
{
    const QStringList &paths = request.paths;
    // A temporary index, copied from the real one, receives the files' state:
    // the real index is not touched.
    const QTemporaryDir temporary;
    if (!temporary.isValid())
        throw GitError(QObject::tr("Cannot create a temporary folder for the snapshot"));
    const QString index = temporary.filePath(QStringLiteral("index"));
    const QString realIndex = QDir(repository.gitDir).filePath(QStringLiteral("index"));
    if (QFileInfo::exists(realIndex))
        copyIndex(realIndex, index);
    QHash<QString, QString> environment = snapshotIdentity();
    environment.insert(QStringLiteral("GIT_INDEX_FILE"), index);

    addExisting(repository, paths, environment);
    settleOtherConflicts(repository, environment);
    const QString tree =
        QString::fromUtf8(Git::run(repository.workDir, {QStringLiteral("write-tree")}, {}, environment)).trimmed();
    if (request.skipIfUnchanged && matchesLastSnapshot(repository, tree, paths, request.wholeWorkingTree))
        return {};

    QStringList commitArguments{QStringLiteral("commit-tree"), QStringLiteral("--no-gpg-sign"), tree};
    if (!repository.unborn)
        commitArguments << QStringLiteral("-p") << QStringLiteral("HEAD");
    commitArguments << QStringLiteral("-F") << QStringLiteral("-");
    const QString message = messageFor(request.reason, paths, request.wholeWorkingTree);
    const QString commit =
        QString::fromUtf8(Git::run(repository.workDir, commitArguments, message.toUtf8(), environment)).trimmed();

    const QString ref = unusedRefName(repository);
    Git::run(repository.workDir, {QStringLiteral("update-ref"), ref, commit});
    prune(repository);
    return ref;
}

// Snapshots taken under one of the editor's former names move to refPrefix,
// with the same name. A reference that cannot be moved stays where it is.
void adoptLegacySnapshots(git_repository *repository, const QString &legacyRefPrefix)
{
    QStringList names;
    {
        git_reference_iterator *iterator = nullptr;
        if (git_reference_iterator_glob_new(&iterator, repository,
                                            (legacyRefPrefix + QLatin1Char('*')).toUtf8().constData()) < 0)
            return;
        const Ptr<git_reference_iterator> iteratorGuard(iterator);
        const char *name = nullptr;
        while (git_reference_next_name(&name, iterator) == 0)
            names.append(QString::fromUtf8(name));
    }
    for (const QString &name : names) {
        git_reference *reference = nullptr;
        if (git_reference_lookup(&reference, repository, name.toUtf8().constData()) < 0)
            continue;
        const Ptr<git_reference> referenceGuard(reference);
        git_reference *renamed = nullptr;
        if (git_reference_rename(&renamed, reference,
                                 (refPrefix + name.mid(legacyRefPrefix.size())).toUtf8().constData(), 0,
                                 "Snapshot of a former name, renamed Kodamiko") == 0)
            git_reference_free(renamed);
    }
}

// Puts `paths` back on disk as `commitId` recorded them, deleting those it
// does not contain.
void restorePaths(const RepositoryInfo &repository, const QString &commitId, const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    const QStringList inSnapshot = Git::splitNul(
        Git::run(repository.workDir, QStringList{QStringLiteral("ls-tree"), QStringLiteral("-r"), QStringLiteral("-z"),
                                                 QStringLiteral("--name-only"), commitId, QStringLiteral("--")} +
                                         paths));
    if (!inSnapshot.isEmpty())
        Git::run(repository.workDir, QStringList{QStringLiteral("restore"), QStringLiteral("--worktree"),
                                                 QStringLiteral("--source=") + commitId, QStringLiteral("--")} +
                                         inSnapshot);
    for (const QString &path : paths) {
        if (!inSnapshot.contains(path))
            GitOperations::removeFile(repository.workDir, path);
    }
}

} // namespace

namespace Snapshots {

QString create(const RepositoryInfo &repository, const QString &reason, const QStringList &paths)
{
    return createSnapshot(repository, {.reason = reason, .paths = paths});
}

QString createOfWorkingTree(const RepositoryInfo &repository, const QString &reason)
{
    return createSnapshot(
        repository,
        {.reason = reason, .paths = changedPaths(repository), .wholeWorkingTree = true, .skipIfUnchanged = true});
}

QString createIfChanged(const RepositoryInfo &repository, const QString &reason)
{
    const QStringList paths = changedPaths(repository);
    return paths.isEmpty()
               ? QString()
               : createSnapshot(repository,
                                {.reason = reason, .paths = paths, .wholeWorkingTree = true, .skipIfUnchanged = true});
}

QList<Snapshot> list(const QString &folder)
{
    const libgit2::Session session;
    git_repository *repository = nullptr;
    const QByteArray folderPath = QDir::toNativeSeparators(folder).toUtf8();
    const int opened = git_repository_open_ext(&repository, folderPath.constData(), 0, nullptr);
    if (opened == GIT_ENOTFOUND)
        return {};
    check(opened, "Opening the git repository");
    const Ptr<git_repository> repositoryGuard(repository);
    for (const QString &legacyRefPrefix : legacyRefPrefixes)
        adoptLegacySnapshots(repository, legacyRefPrefix);

    git_reference_iterator *iterator = nullptr;
    check(git_reference_iterator_glob_new(&iterator, repository, (refPrefix + QLatin1Char('*')).toUtf8().constData()),
          "Reading snapshots");
    const Ptr<git_reference_iterator> iteratorGuard(iterator);

    QList<Snapshot> snapshots;
    git_reference *reference = nullptr;
    while (git_reference_next(&reference, iterator) == 0) {
        const Ptr<git_reference> referenceGuard(reference);
        git_object *object = nullptr;
        if (git_reference_peel(&object, reference, GIT_OBJECT_COMMIT) < 0)
            continue;
        const Ptr<git_object> objectGuard(object);
        const auto *commit = reinterpret_cast<const git_commit *>(object);

        Snapshot snapshot;
        snapshot.ref = QString::fromUtf8(git_reference_name(reference));
        std::array<char, GIT_OID_MAX_HEXSIZE + 1> id{};
        git_oid_tostr(id.data(), id.size(), git_commit_id(commit));
        snapshot.commitId = QString::fromLatin1(id.data());
        snapshot.date = QDateTime::fromSecsSinceEpoch(git_commit_time(commit));
        parseMessage(QString::fromUtf8(git_commit_message(commit)), snapshot);
        snapshots.append(snapshot);
    }
    std::sort(snapshots.begin(), snapshots.end(), [](const Snapshot &a, const Snapshot &b) { return a.ref > b.ref; });
    return snapshots;
}

int restore(const RepositoryInfo &repository, const Snapshot &snapshot)
{
    const QString reason =
        QObject::tr("Before restoring the snapshot of %1").arg(QLocale().toString(snapshot.date, QLocale::ShortFormat));
    if (!snapshot.wholeWorkingTree) {
        if (snapshot.paths.isEmpty())
            return 0;
        create(repository, reason, snapshot.paths);
        restorePaths(repository, snapshot.commitId, snapshot.paths);
        return static_cast<int>(snapshot.paths.size());
    }

    // The current state, recorded whole so that the restore can be undone,
    // gives the files to put back: those that differ from the snapshot.
    const QString current =
        createSnapshot(repository, {.reason = reason, .paths = changedPaths(repository), .wholeWorkingTree = true});
    const QStringList paths =
        Git::splitNul(Git::run(repository.workDir, {QStringLiteral("diff-tree"), QStringLiteral("-r"),
                                                    QStringLiteral("-z"), QStringLiteral("--name-only"),
                                                    QStringLiteral("--no-renames"), snapshot.commitId, current}));
    restorePaths(repository, snapshot.commitId, paths);
    return static_cast<int>(paths.size());
}

} // namespace Snapshots
