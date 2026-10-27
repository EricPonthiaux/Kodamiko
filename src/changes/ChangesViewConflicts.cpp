// SPDX-License-Identifier: GPL-3.0-only

// ChangesView: the files a merge or a rebase left in conflict, resolved
// whole from the list's menu.

#include "ChangesView.h"
#include "git/ConflictMarkers.h"
#include "git/GitCommands.h"
#include "git/GitOperations.h"

#include <QDir>
#include <QFile>
#include <QMenu>
#include <QSet>

namespace {

bool hasConflictMarkers(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return !ConflictMarkers::find(file.readAll()).isEmpty();
}

} // namespace

QList<ChangesView::FileRef> ChangesView::conflictedFiles(const QList<FileRef> &refs) const
{
    // A conflicted file shows on both sides of the index: it counts once.
    QList<FileRef> result;
    QSet<QString> listed;
    for (const FileRef &ref : refs) {
        const FileChange &change = changeOf(ref);
        if (change.kind != ChangeKind::Conflicted || listed.contains(change.path))
            continue;
        listed.insert(change.path);
        result.append(ref);
    }
    return result;
}

void ChangesView::addConflictActions(QMenu &menu, const QList<FileRef> &selection)
{
    const QList<FileRef> conflicted = conflictedFiles(selection);
    if (conflicted.isEmpty())
        return;
    const bool busy = commands_->isBusy();
    menu.addAction(tr("Accept Current Version"), this,
                   [this, conflicted] { acceptVersion(conflicted, Integration::ConflictSide::Current); })
        ->setEnabled(!busy);
    menu.addAction(tr("Accept Incoming Version"), this,
                   [this, conflicted] { acceptVersion(conflicted, Integration::ConflictSide::Incoming); })
        ->setEnabled(!busy);
    menu.addAction(tr("Mark as Resolved"), this, [this, conflicted] { markResolved(conflicted); })->setEnabled(!busy);
    menu.addSeparator();
}

void ChangesView::acceptVersion(const QList<FileRef> &refs, Integration::ConflictSide side)
{
    if (!status_)
        return;
    const QStringList paths = repoPaths(refs);
    commands_->run(tr("Resolving conflicts"), [info = status_->info, paths, side] {
        Integration::acceptVersion(info, paths, side);
        return QObject::tr("%n file(s) resolved.", nullptr, int(paths.size()));
    });
}

void ChangesView::markResolved(const QList<FileRef> &refs)
{
    if (!status_)
        return;
    const QDir folder(folder_);
    for (const FileRef &ref : refs) {
        const QString path = changeOf(ref).path;
        if (hasConflictMarkers(folder.filePath(path))) {
            emit statusMessage(
                tr("%1 still has conflict markers on disk: resolve them and save the file first.").arg(path));
            return;
        }
    }
    commands_->run(tr("Marking as resolved"), [info = status_->info, paths = repoPaths(refs)] {
        GitOperations::stage(info, paths);
        return QString();
    });
}
