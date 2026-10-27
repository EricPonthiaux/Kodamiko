// SPDX-License-Identifier: GPL-3.0-only

#include "FileBlame.h"

#include "LibGit2.h"

#include <QHash>
#include <QObject>
#include <QTimeZone>

#include <array>

using libgit2::check;
using libgit2::Ptr;

namespace {

QString idOf(const git_oid *oid)
{
    std::array<char, GIT_OID_MAX_HEXSIZE + 1> text{};
    git_oid_tostr(text.data(), text.size(), oid);
    return QString::fromLatin1(text.data());
}

// The file's lines in its version at HEAD.
QStringList linesAtHead(git_repository *repository, const QByteArray &path)
{
    git_object *entry = nullptr;
    check(git_revparse_single(&entry, repository, "HEAD:" + path), "Reading the file at HEAD");
    const Ptr<git_object> entryGuard(entry);
    git_blob *blob = nullptr;
    check(git_blob_lookup(&blob, repository, git_object_id(entry)), "Reading the file at HEAD");
    const Ptr<git_blob> blobGuard(blob);
    const QByteArray content(static_cast<const char *>(git_blob_rawcontent(blob)), qsizetype(git_blob_rawsize(blob)));
    QStringList lines = QString::fromUtf8(content).split(QLatin1Char('\n'));
    if (content.endsWith('\n'))
        lines.removeLast(); // after the last line end
    return lines;
}

CommitSummary summaryOf(git_repository *repository, const git_oid *oid)
{
    git_commit *commit = nullptr;
    check(git_commit_lookup(&commit, repository, oid), "Reading a commit of the file");
    const Ptr<git_commit> commitGuard(commit);
    const git_signature *author = git_commit_author(commit);
    CommitSummary summary;
    summary.id = idOf(oid);
    summary.shortId = summary.id.left(7);
    summary.summary = QString::fromUtf8(git_commit_summary(commit));
    summary.message = QString::fromUtf8(git_commit_message(commit)).trimmed();
    summary.author = QString::fromUtf8(author->name);
    summary.email = QString::fromUtf8(author->email);
    summary.date =
        QDateTime::fromSecsSinceEpoch(author->when.time, QTimeZone::fromSecondsAheadOfUtc(author->when.offset * 60));
    return summary;
}

} // namespace

FileBlame FileBlame::read(const QString &folder, const QString &relativePath)
{
    const std::optional<RepositoryInfo> info = RepositoryInfo::read(folder);
    if (!info)
        throw GitError(QObject::tr("This folder is not in a git repository."));
    const libgit2::Session session;
    const Ptr<git_repository> repository = libgit2::openRepository(folder);
    if (!repository)
        throw GitError(QObject::tr("This folder is not in a git repository."));
    const QByteArray path = info->repoPath(relativePath).toUtf8();

    FileBlame result;
    result.lines = linesAtHead(repository.get(), path);
    git_blame_options options;
    git_blame_options_init(&options, GIT_BLAME_OPTIONS_VERSION);
    git_blame *blame = nullptr;
    check(git_blame_file(&blame, repository.get(), path.constData(), &options), "Reading who changed the file");
    const Ptr<git_blame> blameGuard(blame);

    QHash<QString, int> commitIndexes;
    result.lineCommits.reserve(result.lines.size());
    for (size_t i = 0; i < git_blame_hunkcount(blame); ++i) {
        const git_blame_hunk *hunk = git_blame_hunk_byindex(blame, i);
        const QString id = idOf(&hunk->final_commit_id);
        auto index = commitIndexes.constFind(id);
        if (index == commitIndexes.constEnd()) {
            result.commits.append(summaryOf(repository.get(), &hunk->final_commit_id));
            index = commitIndexes.insert(id, int(result.commits.size() - 1));
        }
        result.lineCommits.insert(result.lineCommits.size(), qsizetype(hunk->lines_in_hunk), index.value());
    }
    return result;
}
