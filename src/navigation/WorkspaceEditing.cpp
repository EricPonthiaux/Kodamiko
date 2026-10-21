// SPDX-License-Identifier: GPL-3.0-only

#include "WorkspaceEditing.h"

#include "CodeIntelligence.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "editor/TextFile.h"
#include "lsp/LanguageServers.h"
#include "refactor/WorkspaceFiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <optional>
#include <variant>

using namespace LspProtocol;

namespace WorkspaceEditing {

namespace {

// What is in the way of a file operation's target.
enum class Room {
    Made,     // nothing, or trashed (`overwrite`)
    Occupied, // to keep: an error
    Skip,     // to keep, without an error (`ignoreIfExists`)
};

QString tr(const char *text)
{
    return QCoreApplication::translate("WorkspaceEditing", text);
}

QString nameOf(const QString &path)
{
    return QFileInfo(path).fileName();
}

// The files whose text is no longer the one the server's edits are for.
QStringList staleFiles(const WorkspaceEdit &edit, const EditTargets &targets)
{
    QStringList stale;
    for (const DocumentChange &change : edit.changes) {
        const auto *documentEdit = std::get_if<DocumentEdit>(&change);
        if (!documentEdit || !documentEdit->version)
            continue;
        // As Duet sends it to the servers.
        const std::optional<QString> text = WorkspaceFiles::textOf(documentEdit->filePath, targets);
        const bool isCurrent =
            text && targets.servers->isCurrent(documentEdit->filePath, *documentEdit->version, text->toUtf8());
        if (!isCurrent)
            stale.append(documentEdit->filePath);
    }
    return stale;
}

void editInEditor(EditorWidget *editor, const QList<TextEdit> &edits)
{
    // One undo step; from the end, so that the positions still to change
    // do not move.
    editor->SendScintilla(QsciScintillaBase::SCI_BEGINUNDOACTION);
    for (const TextEdit &textEdit : applicationOrder(edits)) {
        const int start = CodeIntelligence::positionOf(editor, textEdit.range.start);
        const int end = std::max(start, CodeIntelligence::positionOf(editor, textEdit.range.end));
        const QByteArray bytes = TextFile::withLineEnding(textEdit.newText, editor->format().lineEnding).toUtf8();
        editor->SendScintilla(QsciScintillaBase::SCI_SETTARGETRANGE, static_cast<unsigned long>(start), long(end));
        editor->SendScintilla(QsciScintillaBase::SCI_REPLACETARGET, static_cast<uintptr_t>(bytes.size()),
                              bytes.constData());
    }
    editor->SendScintilla(QsciScintillaBase::SCI_ENDUNDOACTION);
}

// Changes the text of `edit`'s file, recording it in `journal`.
QString editText(const DocumentEdit &edit, const EditTargets &targets, EditJournal &journal)
{
    if (EditorWidget *editor = targets.tabs->editorFor(edit.filePath)) {
        const QString before = editor->text();
        editInEditor(editor, edit.edits);
        journal.record(EditJournal::TextChange{edit.filePath, before, editor->text()});
        return {};
    }
    QString error;
    const std::optional<DecodedText> decoded = TextFile::load(edit.filePath, &error);
    if (!decoded)
        return tr("Could not change %1: %2").arg(nameOf(edit.filePath), error);
    QList<TextEdit> edits = edit.edits;
    for (TextEdit &textEdit : edits)
        textEdit.newText = TextFile::withLineEnding(textEdit.newText, decoded->format.lineEnding);
    const QString after = applyEdits(decoded->text, edits);
    if (!TextFile::save(edit.filePath, after, decoded->format, &error))
        return tr("Could not change %1: %2").arg(nameOf(edit.filePath), error);
    journal.record(EditJournal::TextChange{edit.filePath, decoded->text, after});
    return {};
}

Room makeRoom(const FileOperation &operation, const QString &target, const EditTargets &targets, EditJournal &journal)
{
    if (!QFileInfo::exists(target))
        return Room::Made;
    if (!operation.overwrite)
        return operation.ignoreIfExists ? Room::Skip : Room::Occupied;
    QString pathInTrash;
    if (!WorkspaceFiles::trash(target, targets, &pathInTrash).isEmpty())
        return Room::Occupied;
    journal.record(EditJournal::Trashing{target, pathInTrash});
    return Room::Made;
}

QString create(const FileOperation &operation, const EditTargets &targets, EditJournal &journal)
{
    switch (makeRoom(operation, operation.filePath, targets, journal)) {
    case Room::Occupied:
        return tr("Could not create %1: it already exists").arg(nameOf(operation.filePath));
    case Room::Skip:
        return {};
    case Room::Made:
        break;
    }
    if (QString error = WorkspaceFiles::createEmpty(operation.filePath); !error.isEmpty())
        return error;
    journal.record(EditJournal::Creation{operation.filePath});
    return {};
}

QString rename(const FileOperation &operation, const EditTargets &targets, EditJournal &journal)
{
    if (!QFileInfo::exists(operation.filePath))
        return tr("Could not rename %1: it does not exist").arg(nameOf(operation.filePath));
    switch (makeRoom(operation, operation.newPath, targets, journal)) {
    case Room::Occupied:
        return tr("Could not rename %1: %2 already exists").arg(nameOf(operation.filePath), operation.newPath);
    case Room::Skip:
        return {};
    case Room::Made:
        break;
    }
    if (QString error = WorkspaceFiles::move(operation.filePath, operation.newPath, targets); !error.isEmpty())
        return error;
    journal.record(EditJournal::Move{operation.filePath, operation.newPath});
    return {};
}

QString remove(const FileOperation &operation, const EditTargets &targets, EditJournal &journal)
{
    const QFileInfo info(operation.filePath);
    if (!info.exists())
        return operation.ignoreIfNotExists ? QString()
                                           : tr("Could not delete %1: it does not exist").arg(info.fileName());
    const bool isNonEmptyFolder = info.isDir() && !QDir(operation.filePath).isEmpty();
    if (isNonEmptyFolder && !operation.recursive)
        return tr("Could not delete %1: the folder is not empty").arg(info.fileName());
    QString pathInTrash;
    if (QString error = WorkspaceFiles::trash(operation.filePath, targets, &pathInTrash); !error.isEmpty())
        return error;
    journal.record(EditJournal::Trashing{operation.filePath, pathInTrash});
    return {};
}

QString perform(const DocumentChange &change, const EditTargets &targets, EditJournal &journal)
{
    if (const auto *documentEdit = std::get_if<DocumentEdit>(&change))
        return editText(*documentEdit, targets, journal);
    const auto &operation = std::get<FileOperation>(change);
    switch (operation.kind) {
    case FileOperation::Kind::Create:
        return create(operation, targets, journal);
    case FileOperation::Kind::Rename:
        return rename(operation, targets, journal);
    case FileOperation::Kind::Delete:
        return remove(operation, targets, journal);
    }
    return {};
}

// The files the changes of `journal` leave changed, a renamed one counted
// once.
int changedFileCount(const EditJournal &journal)
{
    QSet<QString> changed;
    for (const EditJournal::Entry &entry : journal.entries()) {
        if (const auto *text = std::get_if<EditJournal::TextChange>(&entry)) {
            changed.insert(text->path);
        } else if (const auto *creation = std::get_if<EditJournal::Creation>(&entry)) {
            changed.insert(creation->path);
        } else if (const auto *move = std::get_if<EditJournal::Move>(&entry)) {
            changed.remove(move->from);
            changed.insert(move->to);
        } else {
            changed.insert(std::get<EditJournal::Trashing>(entry).path);
        }
    }
    return int(changed.size());
}

} // namespace

Outcome apply(const WorkspaceEdit &edit, const EditTargets &targets)
{
    Outcome outcome;
    if (const QStringList stale = staleFiles(edit, targets); !stale.isEmpty()) {
        for (const QString &path : stale)
            outcome.errors.append(tr("%1 changed meanwhile: nothing was changed, try again").arg(nameOf(path)));
        return outcome;
    }
    EditJournal journal;
    for (const DocumentChange &change : edit.changes) {
        const QString error = perform(change, targets, journal);
        if (error.isEmpty())
            continue;
        outcome.errors.append(error);
        // All or nothing.
        outcome.errors.append(journal.revert(targets));
        if (!journal.isEmpty())
            outcome.errors.append(tr("The changes already made were undone"));
        return outcome;
    }
    outcome.filesChanged = changedFileCount(journal);
    outcome.journal = std::move(journal);
    return outcome;
}

} // namespace WorkspaceEditing
