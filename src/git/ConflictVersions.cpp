// SPDX-License-Identifier: GPL-3.0-only

#include "ConflictVersions.h"

#include "LibGit2.h"

#include <QObject>

#include <algorithm>

namespace ConflictVersions {
namespace {

using libgit2::Ptr;

// A version as the index names it, before its content is read.
struct Stage {
    QString path;
    git_oid id{};
};

// The index's entries of one conflicted path.
struct Entry {
    std::optional<Stage> base;
    std::optional<Stage> current;
    std::optional<Stage> incoming;
};

std::optional<Stage> stageOf(const git_index_entry *entry)
{
    if (!entry)
        return std::nullopt;
    return Stage{QString::fromUtf8(entry->path), entry->id};
}

QList<Entry> readEntries(git_index *index)
{
    git_index_conflict_iterator *iterator = nullptr;
    libgit2::check(git_index_conflict_iterator_new(&iterator, index), "Reading the conflicts");
    const Ptr<git_index_conflict_iterator> iteratorGuard(iterator);
    QList<Entry> entries;
    const git_index_entry *ancestor = nullptr;
    const git_index_entry *ours = nullptr;
    const git_index_entry *theirs = nullptr;
    int result = 0;
    while ((result = git_index_conflict_next(&ancestor, &ours, &theirs, iterator)) == 0)
        entries.append({stageOf(ancestor), stageOf(ours), stageOf(theirs)});
    if (result != GIT_ITEROVER)
        libgit2::check(result, "Reading the conflicts");
    return entries;
}

bool hasOnly(const Entry &entry, const std::optional<Stage> Entry::*stage)
{
    const int count = int(entry.base.has_value()) + int(entry.current.has_value()) + int(entry.incoming.has_value());
    return count == 1 && (entry.*stage).has_value();
}

// The entry, among `entries`, that holds only `stage` with the id `id`.
qsizetype findAlone(const QList<Entry> &entries, const std::optional<Stage> Entry::*stage, const git_oid &id)
{
    const auto found = std::ranges::find_if(entries, [stage, &id](const Entry &entry) {
        return hasOnly(entry, stage) && git_oid_equal(&(entry.*stage)->id, &id);
    });
    return found == entries.end() ? -1 : found - entries.begin();
}

// Joins the three entries a rename on both sides leaves (the ancestor's
// path, then each side's) into one, when the content is the same in all.
QList<Entry> pairRenames(QList<Entry> entries)
{
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const std::optional<Stage> &base = entries[i].base;
        if (!base || !hasOnly(entries[i], &Entry::base))
            continue;
        const git_oid id = base->id;
        const qsizetype current = findAlone(entries, &Entry::current, id);
        const qsizetype incoming = findAlone(entries, &Entry::incoming, id);
        if (current < 0 || incoming < 0)
            continue;
        entries[i].current = entries[current].current;
        entries[i].incoming = entries[incoming].incoming;
        entries.removeAt(std::max(current, incoming));
        entries.removeAt(std::min(current, incoming));
        if (std::min(current, incoming) < i)
            --i;
    }
    return entries;
}

Kind kindOf(const Entry &entry)
{
    const bool hasBase = entry.base.has_value();
    const bool hasCurrent = entry.current.has_value();
    const bool hasIncoming = entry.incoming.has_value();
    if (hasCurrent && hasIncoming) {
        if (!hasBase)
            return Kind::BothAdded;
        const bool isRenamed = entry.current->path != entry.base->path || entry.incoming->path != entry.base->path;
        return isRenamed ? Kind::RenamedOnBothSides : Kind::BothModified;
    }
    if (hasBase && hasIncoming)
        return Kind::DeletedByCurrent;
    if (hasBase && hasCurrent)
        return Kind::DeletedByIncoming;
    if (hasCurrent)
        return Kind::AddedByCurrent;
    if (hasIncoming)
        return Kind::AddedByIncoming;
    return Kind::DeletedByBoth;
}

std::optional<Version> versionOf(git_repository *repository, const std::optional<Stage> &stage)
{
    if (!stage)
        return std::nullopt;
    git_blob *blob = nullptr;
    libgit2::check(git_blob_lookup(&blob, repository, &stage->id), "Reading a conflicted file");
    const Ptr<git_blob> blobGuard(blob);
    const QByteArray content(static_cast<const char *>(git_blob_rawcontent(blob)), qsizetype(git_blob_rawsize(blob)));
    return Version{stage->path, content};
}

bool isBinary(const std::optional<Version> &version)
{
    constexpr qsizetype binaryCheckBytes = 8000;
    return version && version->content.first(std::min(version->content.size(), binaryCheckBytes)).contains('\0');
}

// The ancestor's path, or the path of the only side that has the file.
QString pathOf(const File &file)
{
    if (file.base)
        return file.base->path;
    if (file.current)
        return file.current->path;
    return file.incoming ? file.incoming->path : QString();
}

File fileOf(git_repository *repository, const Entry &entry)
{
    File file;
    file.kind = kindOf(entry);
    file.base = versionOf(repository, entry.base);
    file.current = versionOf(repository, entry.current);
    file.incoming = versionOf(repository, entry.incoming);
    file.path = pathOf(file);
    file.isBinary = isBinary(file.base) || isBinary(file.current) || isBinary(file.incoming);
    return file;
}

} // namespace

QList<File> read(const RepositoryInfo &repository)
{
    const libgit2::Session session;
    const Ptr<git_repository> opened = libgit2::openRepository(repository.workDir);
    if (!opened)
        throw GitError(QObject::tr("%1 is not in a git repository").arg(repository.workDir));
    git_index *index = nullptr;
    libgit2::check(git_repository_index(&index, opened.get()), "Reading the index");
    const Ptr<git_index> indexGuard(index);
    QList<File> files;
    for (const Entry &entry : pairRenames(readEntries(index)))
        files.append(fileOf(opened.get(), entry));
    std::ranges::sort(files, {}, &File::path);
    return files;
}

} // namespace ConflictVersions
