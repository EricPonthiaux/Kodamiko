// SPDX-License-Identifier: GPL-3.0-only

#include "FileUndoStack.h"

#include "FileOperations.h"
#include "FileTreeView.h"

#include <QFile>
#include <QFileInfo>

namespace {

// Older steps are forgotten: nobody undoes that far back.
constexpr std::size_t maximumSteps = 100;

QString moveToTrash(const QString &path)
{
    if (!QFile::moveToTrash(path))
        return FileTreeView::tr("Cannot move “%1” to the trash").arg(QFileInfo(path).fileName());
    return {};
}

bool isRename(const FileUndoStack::Step &step)
{
    return QFileInfo(step.source).absolutePath() == QFileInfo(step.path).absolutePath();
}

} // namespace

void FileUndoStack::record(Step step)
{
    steps_.push_back(std::move(step));
    if (steps_.size() > maximumSteps)
        steps_.erase(steps_.begin());
}

std::optional<FileUndoStack::Step> FileUndoStack::last() const
{
    if (steps_.empty())
        return std::nullopt;
    return steps_.back();
}

QString FileUndoStack::lastDescription() const
{
    if (steps_.empty())
        return {};
    const Step &step = steps_.back();
    const QString name = QFileInfo(step.path).fileName();
    switch (step.kind) {
    case Step::Kind::Created:
        return FileTreeView::tr("Undo Creation of “%1”").arg(name);
    case Step::Kind::Moved:
        return isRename(step) ? FileTreeView::tr("Undo Rename of “%1”").arg(QFileInfo(step.source).fileName())
                              : FileTreeView::tr("Undo Move of “%1”").arg(name);
    case Step::Kind::Trashed:
        return FileTreeView::tr("Undo Deletion of “%1”").arg(name);
    }
    return {};
}

QString FileUndoStack::undo()
{
    if (steps_.empty())
        return {};
    const Step step = steps_.back();
    steps_.pop_back();
    switch (step.kind) {
    case Step::Kind::Created:
        return moveToTrash(step.path);
    case Step::Kind::Moved:
        return FileOperations::move(step.path, step.source);
    case Step::Kind::Trashed:
        return FileOperations::takeBackFromTrash(step.source, step.path);
    }
    return {};
}
