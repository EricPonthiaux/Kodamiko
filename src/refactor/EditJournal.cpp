// SPDX-License-Identifier: GPL-3.0-only

#include "EditJournal.h"

#include "WorkspaceFiles.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
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

// Where `path` was before the `earlierMoves` (earliest first).
QString originalPathOf(QString path, const QList<EditJournal::Move> &earlierMoves)
{
    for (const EditJournal::Move &move : std::views::reverse(earlierMoves)) {
        if (WorkspaceFiles::isUnder(path, move.to))
            path = move.from + path.mid(move.to.size());
    }
    return path;
}

// Takes `editor` to `text` through its own undo, if its last step leads
// there: its history stays as if that step had never been made, and its
// redo can make it again. Otherwise leaves it as it was.
bool undoesTo(EditorWidget *editor, const QString &text)
{
    if (!editor->isUndoAvailable())
        return false;
    editor->undo();
    if (editor->text() == text)
        return true;
    editor->redo();
    return false;
}

// undoesTo(), the other way.
bool redoesTo(EditorWidget *editor, const QString &text)
{
    if (!editor->isRedoAvailable())
        return false;
    editor->redo();
    if (editor->text() == text)
        return true;
    editor->undo();
    return false;
}

QString replayEntry(const EditJournal::TextChange &change, const EditTargets &targets)
{
    EditorWidget *editor = targets.tabs->editorFor(change.path);
    if (editor && redoesTo(editor, change.after))
        return {};
    return WorkspaceFiles::setText(change.path, change.after, targets);
}

QString replayEntry(const EditJournal::Creation &creation, const EditTargets &)
{
    return WorkspaceFiles::createEmpty(creation.path);
}

QString replayEntry(const EditJournal::Move &move, const EditTargets &targets)
{
    return WorkspaceFiles::move(move.from, move.to, targets);
}

QString replayEntry(EditJournal::Trashing &trashing, const EditTargets &targets)
{
    return WorkspaceFiles::trash(trashing.path, targets, &trashing.pathInTrash);
}

QString revertEntry(const EditJournal::TextChange &change, const EditTargets &targets)
{
    EditorWidget *editor = targets.tabs->editorFor(change.path);
    if (editor && undoesTo(editor, change.before))
        return {};
    return WorkspaceFiles::setText(change.path, change.before, targets);
}

QString revertEntry(const EditJournal::Creation &creation, const EditTargets &targets)
{
    const QFileInfo info(creation.path);
    if (!info.exists())
        return {};
    // Empty again once its texts are reverted, in its editor or on disk:
    // nothing in it to keep in the trash.
    const bool isEmptyAgain = info.isFile() && WorkspaceFiles::textOf(creation.path, targets) == QString();
    if (isEmptyAgain)
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

QStringList EditJournal::changedSinceRevert(const EditTargets &targets) const
{
    QStringList changed;
    QList<Move> earlierMoves;
    for (const Entry &entry : entries_) {
        if (const auto *move = std::get_if<Move>(&entry)) {
            earlierMoves.append(*move);
            continue;
        }
        const auto *change = std::get_if<TextChange>(&entry);
        if (!change)
            continue;
        const QString path = originalPathOf(change->path, earlierMoves);
        if (!QFileInfo::exists(path))
            continue; // created by the refactoring: it comes back as it was
        const bool isUnchanged = WorkspaceFiles::textOf(path, targets) == change->before;
        if (!isUnchanged && !changed.contains(path))
            changed.append(path);
    }
    return changed;
}

QStringList EditJournal::replay(const EditTargets &targets)
{
    QStringList errors;
    for (Entry &entry : entries_) {
        const QString error = std::visit([&targets](auto &change) { return replayEntry(change, targets); }, entry);
        if (!error.isEmpty())
            errors.append(error);
    }
    return errors;
}

std::optional<QString> EditJournal::textAfter(const QString &path) const
{
    QList<Move> laterMoves;
    for (const Entry &entry : std::views::reverse(entries_)) {
        if (const auto *move = std::get_if<Move>(&entry)) {
            laterMoves.append(*move);
            continue;
        }
        const auto *change = std::get_if<TextChange>(&entry);
        if (change && currentPathOf(change->path, laterMoves) == path)
            return change->after;
    }
    return std::nullopt;
}

std::optional<QString> EditJournal::textBefore(const QString &path) const
{
    QList<Move> earlierMoves;
    for (const Entry &entry : entries_) {
        if (const auto *move = std::get_if<Move>(&entry)) {
            earlierMoves.append(*move);
            continue;
        }
        const auto *change = std::get_if<TextChange>(&entry);
        if (change && originalPathOf(change->path, earlierMoves) == path)
            return change->before;
    }
    return std::nullopt;
}
