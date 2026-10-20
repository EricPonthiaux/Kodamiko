// SPDX-License-Identifier: GPL-3.0-only

#include "LineChanges.h"

#include "LibGit2.h"

#include <QDir>
#include <QFileInfo>

using libgit2::check;
using libgit2::Ptr;

namespace {

// Hunk side as git numbers it (from 1, the preceding line when the side is
// empty) → first line from 0, the insertion point when the side is empty.
int firstLine(int start, int count)
{
    return count > 0 ? start - 1 : start;
}

int collectHunk(const git_diff_delta *, const git_diff_hunk *hunk, void *payload)
{
    auto *changes = static_cast<QList<LineChange> *>(payload);
    LineChange change;
    change.baseCount = hunk->old_lines;
    change.baseStart = firstLine(hunk->old_start, hunk->old_lines);
    change.count = hunk->new_lines;
    change.start = firstLine(hunk->new_start, hunk->new_lines);
    changes->append(change);
    return 0;
}

} // namespace

LineChange::Kind LineChange::kind() const
{
    if (baseCount == 0)
        return Kind::Added;
    return count == 0 ? Kind::Deleted : Kind::Modified;
}

bool LineChange::covers(int line, int lineCount) const
{
    if (count > 0)
        return line >= start && line < start + count;
    return line == std::min(start, std::max(lineCount - 1, 0));
}

namespace LineChanges {

QList<LineChange> compute(const QByteArray &base, const QByteArray &current)
{
    const libgit2::Session session;
    git_diff_options options;
    git_diff_options_init(&options, GIT_DIFF_OPTIONS_VERSION);
    options.context_lines = 0;
    options.interhunk_lines = 0;
    options.flags = GIT_DIFF_FORCE_TEXT;
    QList<LineChange> changes;
    check(git_diff_buffers(base.constData(), static_cast<size_t>(base.size()), nullptr, current.constData(),
                           static_cast<size_t>(current.size()), nullptr, &options, nullptr, nullptr, collectHunk,
                           nullptr, &changes),
          "Comparing with the index");
    return changes;
}

qsizetype lineOffset(const QByteArray &text, int line)
{
    qsizetype offset = 0;
    for (int i = 0; i < line; ++i) {
        const qsizetype end = text.indexOf('\n', offset);
        if (end < 0)
            return text.size();
        offset = end + 1;
    }
    return offset;
}

QByteArray lines(const QByteArray &text, int start, int count)
{
    const qsizetype from = lineOffset(text, start);
    const qsizetype to = lineOffset(text, start + count);
    return text.mid(from, to - from);
}

QByteArray revert(const QByteArray &base, const QByteArray &current, const LineChange &change)
{
    const qsizetype from = lineOffset(current, change.start);
    const qsizetype to = lineOffset(current, change.start + change.count);
    QByteArray result = current;
    result.replace(from, to - from, lines(base, change.baseStart, change.baseCount));
    return result;
}

std::optional<QByteArray> indexVersion(const QString &absolutePath)
{
    const libgit2::Session session;
    const QFileInfo file(absolutePath);
    const Ptr<git_repository> repository = libgit2::openRepository(file.absolutePath());
    if (!repository)
        return std::nullopt;
    const QString workDir = QFileInfo(QString::fromUtf8(git_repository_workdir(repository.get()))).canonicalFilePath();
    const QString canonical = file.canonicalFilePath();
    if (canonical.isEmpty())
        return std::nullopt;
    const QString path = QDir(workDir).relativeFilePath(canonical);
    if (path.startsWith(QLatin1String("..")))
        return std::nullopt;
    const QByteArray repoPath = path.toUtf8();

    git_index *index = nullptr;
    check(git_repository_index(&index, repository.get()), "Reading the index");
    const Ptr<git_index> indexGuard(index);
    const git_index_entry *entry = git_index_get_bypath(index, repoPath.constData(), 0);
    if (!entry)
        return std::nullopt;

    git_blob *blob = nullptr;
    check(git_blob_lookup(&blob, repository.get(), &entry->id), "Reading the indexed file");
    const Ptr<git_blob> blobGuard(blob);
    git_blob_filter_options filterOptions;
    git_blob_filter_options_init(&filterOptions, GIT_BLOB_FILTER_OPTIONS_VERSION);
    git_buf buffer = GIT_BUF_INIT;
    check(git_blob_filter(&buffer, blob, repoPath.constData(), &filterOptions), "Reading the indexed file");
    QByteArray content(buffer.ptr, static_cast<qsizetype>(buffer.size));
    git_buf_dispose(&buffer);
    return content;
}

} // namespace LineChanges
