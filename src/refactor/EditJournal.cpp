// SPDX-License-Identifier: GPL-3.0-only

#include "EditJournal.h"

#include "WorkspaceFiles.h"
#include "workspace/FileOperations.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <ranges>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("EditJournal", text);
}

// Where `path` is now, after the `laterMoves` (latest first).
QString currentPathOf(QString path, const QList<EditJournal::Move> &laterMoves)
{
    for (const EditJournal::Move &move : std::views::reverse(laterMoves)) {
        if (WorkspaceFiles::isUnder(path, move.from))
            path = move.to + path.mid(move.from.size());
    }
    return path;
}

QString revertEntry(const EditJournal::TextChange &change, const EditTargets &targets)
{
    return WorkspaceFiles::setText(change.path, change.before, targets);
}

QString revertEntry(const EditJournal::Creation &creation, const EditTargets &targets)
{
    const QFileInfo info(creation.path);
    if (!info.exists())
        return {};
    // Empty again once its texts are reverted: nothing to keep in the trash.
    if (info.isFile() && info.size() == 0)
        return WorkspaceFiles::remove(creation.path, targets);
    QString pathInTrash;
    return WorkspaceFiles::trash(creation.path, targets, &pathInTrash);
}

QString revertEntry(const EditJournal::Move &move, const EditTargets &targets)
{
    return WorkspaceFiles::move(move.to, move.from, targets);
}

QString revertEntry(const EditJournal::Trashing &trashing, const EditTargets &)
{
    if (trashing.pathInTrash.isEmpty())
        return tr("%1 cannot be taken back from the trash").arg(QFileInfo(trashing.path).fileName());
    return FileOperations::takeBackFromTrash(trashing.pathInTrash, trashing.path);
}

} // namespace

QStringList EditJournal::changedSince(const EditTargets &targets) const
{
    QStringList changed;
    QList<Move> laterMoves;
    for (const Entry &entry : std::views::reverse(entries_)) {
        if (const auto *move = std::get_if<Move>(&entry)) {
            laterMoves.append(*move);
            continue;
        }
        const auto *change = std::get_if<TextChange>(&entry);
        if (!change)
            continue;
        const QString path = currentPathOf(change->path, laterMoves);
        if (!QFileInfo::exists(path))
            continue; // trashed later: it comes back as it went
        const bool isUnchanged = WorkspaceFiles::textOf(path, targets) == change->after;
        if (!isUnchanged && !changed.contains(path))
            changed.append(path);
    }
    return changed;
}

QStringList EditJournal::revert(const EditTargets &targets) const
{
    QStringList errors;
    for (const Entry &entry : std::views::reverse(entries_)) {
        const QString error =
            std::visit([&targets](const auto &change) { return revertEntry(change, targets); }, entry);
        if (!error.isEmpty())
            errors.append(error);
    }
    return errors;
}
