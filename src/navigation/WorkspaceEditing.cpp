// SPDX-License-Identifier: GPL-3.0-only

#include "WorkspaceEditing.h"

#include "CodeIntelligence.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "editor/TextFile.h"
#include "lsp/LanguageServers.h"
#include "workspace/FileOperations.h"

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

QString tr(const char *text)
{
    return QCoreApplication::translate("WorkspaceEditing", text);
}

QString nameOf(const QString &path)
{
    return QFileInfo(path).fileName();
}

// The server writes "\n"; files with other line endings keep theirs.
QString withLineEnding(QString text, LineEnding lineEnding)
{
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    if (lineEnding == LineEnding::CrLf)
        text.replace(QLatin1Char('\n'), QLatin1String("\r\n"));
    else if (lineEnding == LineEnding::Cr)
        text.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    return text;
}

// The text of `path` as Duet sends it to the servers: its editor's, or
// else the file's; nullopt if it cannot be read.
std::optional<QByteArray> currentText(const QString &path, const EditorTabs &tabs)
{
    if (const EditorWidget *editor = tabs.editorFor(path))
        return editor->utf8Bytes().toByteArray();
    QString error;
    const std::optional<DecodedText> decoded = TextFile::load(path, &error);
    if (!decoded)
        return std::nullopt;
    return decoded->text.toUtf8();
}

// The files whose text is no longer the one the server's edits are for.
QStringList staleFiles(const WorkspaceEdit &edit, const Targets &targets)
{
    QStringList stale;
    for (const DocumentChange &change : edit.changes) {
        const auto *documentEdit = std::get_if<DocumentEdit>(&change);
        if (!documentEdit || !documentEdit->version)
            continue;
        const std::optional<QByteArray> text = currentText(documentEdit->filePath, *targets.tabs);
        const bool isCurrent =
            text && targets.servers->isCurrent(documentEdit->filePath, *documentEdit->version, *text);
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
        const QByteArray bytes = withLineEnding(textEdit.newText, editor->format().lineEnding).toUtf8();
        editor->SendScintilla(QsciScintillaBase::SCI_SETTARGETRANGE, static_cast<unsigned long>(start), long(end));
        editor->SendScintilla(QsciScintillaBase::SCI_REPLACETARGET, static_cast<uintptr_t>(bytes.size()),
                              bytes.constData());
    }
    editor->SendScintilla(QsciScintillaBase::SCI_ENDUNDOACTION);
}

// Returns an error message, empty on success.
QString editOnDisk(const QString &path, QList<TextEdit> edits)
{
    QString error;
    const std::optional<DecodedText> decoded = TextFile::load(path, &error);
    if (!decoded)
        return tr("Could not change %1: %2").arg(nameOf(path), error);
    for (TextEdit &textEdit : edits)
        textEdit.newText = withLineEnding(textEdit.newText, decoded->format.lineEnding);
    if (!TextFile::save(path, applyEdits(decoded->text, edits), decoded->format, &error))
        return tr("Could not change %1: %2").arg(nameOf(path), error);
    return {};
}

bool isUnder(const QString &path, const QString &folder)
{
    return path == folder || path.startsWith(folder + QLatin1Char('/'));
}

// The servers read what is under `folder` (or the file itself) from disk
// again.
void closeDocumentsUnder(const QString &folder, LanguageServers &servers)
{
    for (const QString &document : servers.openDocuments()) {
        if (isUnder(document, folder))
            servers.closeDocument(document);
    }
}

// Trashes `path` and closes its tabs; returns an error message, empty on
// success.
QString trash(const QString &path, const Targets &targets)
{
    targets.tabs->closeFilesUnder(path);
    closeDocumentsUnder(path, *targets.servers);
    if (!QFile::moveToTrash(path))
        return tr("Could not move %1 to the trash").arg(nameOf(path));
    return {};
}

QString create(const FileOperation &operation, const Targets &targets)
{
    if (QFileInfo::exists(operation.filePath)) {
        if (operation.ignoreIfExists && !operation.overwrite)
            return {};
        if (!operation.overwrite)
            return tr("Could not create %1: it already exists").arg(nameOf(operation.filePath));
        if (const QString error = trash(operation.filePath, targets); !error.isEmpty())
            return error;
    }
    QDir().mkpath(QFileInfo(operation.filePath).absolutePath());
    QFile file(operation.filePath);
    if (!file.open(QIODevice::WriteOnly))
        return tr("Could not create %1: %2").arg(nameOf(operation.filePath), file.errorString());
    return {};
}

// The editor of `path`'s unsaved changes go with it.
QString saveBeforeMoving(const QString &path, const EditorTabs &tabs)
{
    if (!tabs.hasUnsavedChangesUnder(path))
        return {};
    EditorWidget *editor = tabs.editorFor(path);
    if (!editor)
        return tr("Could not rename %1: save the files it holds first").arg(nameOf(path));
    QString error;
    if (!editor->save(&error))
        return tr("Could not save %1 before renaming it: %2").arg(nameOf(path), error);
    return {};
}

QString rename(const FileOperation &operation, const Targets &targets)
{
    if (!QFileInfo::exists(operation.filePath))
        return tr("Could not rename %1: it does not exist").arg(nameOf(operation.filePath));
    if (QFileInfo::exists(operation.newPath)) {
        if (operation.ignoreIfExists && !operation.overwrite)
            return {};
        if (!operation.overwrite)
            return tr("Could not rename %1: %2 already exists").arg(nameOf(operation.filePath), operation.newPath);
        if (const QString error = trash(operation.newPath, targets); !error.isEmpty())
            return error;
    }
    if (const QString error = saveBeforeMoving(operation.filePath, *targets.tabs); !error.isEmpty())
        return error;
    QDir().mkpath(QFileInfo(operation.newPath).absolutePath());
    if (const QString error = FileOperations::move(operation.filePath, operation.newPath); !error.isEmpty())
        return error;
    closeDocumentsUnder(operation.filePath, *targets.servers);
    targets.tabs->followRename(operation.filePath, operation.newPath);
    return {};
}

QString remove(const FileOperation &operation, const Targets &targets)
{
    const QFileInfo info(operation.filePath);
    if (!info.exists())
        return operation.ignoreIfNotExists ? QString()
                                           : tr("Could not delete %1: it does not exist").arg(info.fileName());
    const bool isNonEmptyFolder = info.isDir() && !QDir(operation.filePath).isEmpty();
    if (isNonEmptyFolder && !operation.recursive)
        return tr("Could not delete %1: the folder is not empty").arg(info.fileName());
    return trash(operation.filePath, targets);
}

QString perform(const FileOperation &operation, const Targets &targets)
{
    switch (operation.kind) {
    case FileOperation::Kind::Create:
        return create(operation, targets);
    case FileOperation::Kind::Rename:
        return rename(operation, targets);
    case FileOperation::Kind::Delete:
        return remove(operation, targets);
    }
    return {};
}

struct ChangeResult {
    QString path; // what the change leaves changed
    QString error;
    QString formerPath = {}; // renamed: the same file as `path`
};

ChangeResult applyChange(const DocumentChange &change, const Targets &targets)
{
    if (const auto *documentEdit = std::get_if<DocumentEdit>(&change)) {
        if (EditorWidget *editor = targets.tabs->editorFor(documentEdit->filePath)) {
            editInEditor(editor, documentEdit->edits);
            return {documentEdit->filePath, {}};
        }
        return {documentEdit->filePath, editOnDisk(documentEdit->filePath, documentEdit->edits)};
    }
    const auto &operation = std::get<FileOperation>(change);
    if (operation.kind == FileOperation::Kind::Rename)
        return {operation.newPath, perform(operation, targets), operation.filePath};
    return {operation.filePath, perform(operation, targets)};
}

} // namespace

Outcome apply(const WorkspaceEdit &edit, const Targets &targets)
{
    Outcome outcome;
    if (const QStringList stale = staleFiles(edit, targets); !stale.isEmpty()) {
        for (const QString &path : stale)
            outcome.errors.append(tr("%1 changed meanwhile: nothing was changed, try again").arg(nameOf(path)));
        return outcome;
    }
    QSet<QString> changed;
    for (const DocumentChange &change : edit.changes) {
        const ChangeResult result = applyChange(change, targets);
        if (!result.error.isEmpty()) {
            outcome.errors.append(result.error);
            continue;
        }
        changed.remove(result.formerPath);
        changed.insert(result.path);
    }
    outcome.filesChanged = int(changed.size());
    return outcome;
}

} // namespace WorkspaceEditing
