// SPDX-License-Identifier: GPL-3.0-only

#include "MergeView.h"
#include "git/GitProcess.h"

#include <QDir>
#include <QFile>

#include <algorithm>

void MergeView::reload()
{
    pool_.start([this, folder = folder_, previous = conflicts_] {
        const Conflicts conflicts = readConflicts(folder, previous);
        QMetaObject::invokeMethod(this, [this, conflicts] { showConflicts(conflicts); }, Qt::QueuedConnection);
    });
}

MergeView::Conflicts MergeView::readConflicts(const QString &folder, const Conflicts &previous)
{
    Conflicts conflicts;
    try {
        const auto repository = RepositoryInfo::read(folder);
        if (!repository) {
            conflicts.error = tr("This folder is not in a git repository.");
            return conflicts;
        }
        conflicts.repository = *repository;
        conflicts.files = ConflictVersions::read(*repository);
        conflicts.sides = Integration::conflictSides(*repository);
        conflicts.paths =
            Git::splitNul(Git::run(repository->workDir, {QStringLiteral("diff"), QStringLiteral("--cached"),
                                                         QStringLiteral("--name-only"), QStringLiteral("-z")}));
        QFile heads(QDir(repository->gitDir).filePath(QStringLiteral("MERGE_HEAD")));
        if (heads.open(QIODevice::ReadOnly))
            conflicts.mergeHeads = QString::fromUtf8(heads.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        conflicts.completedMerge = completesMerge(previous, conflicts);
        if (conflicts.completedMerge)
            conflicts.sides = previous.sides;
        if (conflicts.files.isEmpty() && Integration::canContinue(repository->operation))
            readReadyState(conflicts);
    } catch (const GitError &error) {
        conflicts.error = error.message();
    }
    return conflicts;
}

void MergeView::readReadyState(Conflicts &conflicts)
{
    const RepositoryInfo &repository = conflicts.repository;
    if (const auto status = RepositoryStatus::read(repository.workDir))
        conflicts.staged = status->staged;
    QFile message(QDir(repository.gitDir).filePath(QStringLiteral("MERGE_MSG")));
    if (message.open(QIODevice::ReadOnly)) {
        conflicts.message =
            QString::fromUtf8(Git::run(repository.workDir,
                                       {QStringLiteral("stripspace"), QStringLiteral("--strip-comments")},
                                       message.readAll()))
                .trimmed();
    }
}

bool MergeView::completesMerge(const Conflicts &previous, const Conflicts &current)
{
    if (previous.repository.operation != tr("merge") || previous.mergeHeads.isEmpty())
        return false;
    if (!current.repository.operation.isEmpty() || !current.files.isEmpty())
        return false;
    if (current.repository.headId == previous.repository.headId)
        return false; // An abort restores the original HEAD.
    const QStringList parents =
        QString::fromUtf8(Git::run(current.repository.workDir, {QStringLiteral("show"), QStringLiteral("--no-patch"),
                                                                QStringLiteral("--format=%P"), QStringLiteral("HEAD")}))
            .trimmed()
            .split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parents.size() < 2 || !parents.first().startsWith(previous.repository.headId))
        return false;
    return std::ranges::all_of(previous.mergeHeads, [&parents](const QString &head) { return parents.contains(head); });
}
