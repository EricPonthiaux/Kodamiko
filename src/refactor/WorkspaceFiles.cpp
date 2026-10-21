// SPDX-License-Identifier: GPL-3.0-only

#include "WorkspaceFiles.h"

#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "editor/TextFile.h"
#include "lsp/LanguageServers.h"
#include "workspace/FileOperations.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace WorkspaceFiles {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("WorkspaceFiles", text);
}

QString nameOf(const QString &path)
{
    return QFileInfo(path).fileName();
}

// The servers read what is under `folder` (or the file itself) from disk
// again.
void closeDocumentsUnder(const QString &folder, LanguageServers *servers)
{
    if (!servers)
        return;
    for (const QString &document : servers->openDocuments()) {
        if (isUnder(document, folder))
            servers->closeDocument(document);
    }
}

void replaceText(const EditorWidget *editor, const QString &text)
{
    const QByteArray bytes = text.toUtf8();
    editor->SendScintilla(QsciScintillaBase::SCI_BEGINUNDOACTION);
    editor->SendScintilla(QsciScintillaBase::SCI_SETTARGETRANGE, 0UL,
                          editor->SendScintilla(QsciScintillaBase::SCI_GETLENGTH));
    editor->SendScintilla(QsciScintillaBase::SCI_REPLACETARGET, static_cast<uintptr_t>(bytes.size()),
                          bytes.constData());
    editor->SendScintilla(QsciScintillaBase::SCI_ENDUNDOACTION);
}

// The unsaved changes of `path`'s editor go with the file.
QString saveBeforeMoving(const QString &path, const EditorTabs &tabs)
{
    if (!tabs.hasUnsavedChangesUnder(path))
        return {};
    EditorWidget *editor = tabs.editorFor(path);
    if (!editor)
        return tr("Could not move %1: save the files it holds first").arg(nameOf(path));
    QString error;
    if (!editor->save(&error))
        return tr("Could not save %1 before moving it: %2").arg(nameOf(path), error);
    return {};
}

} // namespace

bool isUnder(const QString &path, const QString &folder)
{
    return path == folder || path.startsWith(folder + QLatin1Char('/'));
}

std::optional<QString> textOf(const QString &path, const EditTargets &targets)
{
    if (const EditorWidget *editor = targets.tabs->editorFor(path))
        return editor->text();
    QString error;
    std::optional<DecodedText> decoded = TextFile::load(path, &error);
    if (!decoded)
        return std::nullopt;
    return std::move(decoded->text);
}

QString setText(const QString &path, const QString &text, const EditTargets &targets)
{
    if (const EditorWidget *editor = targets.tabs->editorFor(path)) {
        replaceText(editor, text);
        return {};
    }
    QString error;
    const std::optional<DecodedText> decoded = TextFile::load(path, &error);
    if (!decoded)
        return tr("Could not change %1: %2").arg(nameOf(path), error);
    if (!TextFile::save(path, text, decoded->format, &error))
        return tr("Could not change %1: %2").arg(nameOf(path), error);
    return {};
}

QString move(const QString &from, const QString &to, const EditTargets &targets)
{
    if (const QString error = saveBeforeMoving(from, *targets.tabs); !error.isEmpty())
        return error;
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (const QString error = FileOperations::move(from, to); !error.isEmpty())
        return error;
    closeDocumentsUnder(from, targets.servers);
    targets.tabs->followRename(from, to);
    return {};
}

QString trash(const QString &path, const EditTargets &targets, QString *pathInTrash)
{
    targets.tabs->closeFilesUnder(path);
    closeDocumentsUnder(path, targets.servers);
    if (!QFile::moveToTrash(path, pathInTrash))
        return tr("Could not move %1 to the trash").arg(nameOf(path));
    return {};
}

QString remove(const QString &path, const EditTargets &targets)
{
    targets.tabs->closeFilesUnder(path);
    closeDocumentsUnder(path, targets.servers);
    if (!QFile::remove(path))
        return tr("Could not delete %1").arg(nameOf(path));
    return {};
}

QString createEmpty(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return tr("Could not create %1: %2").arg(nameOf(path), file.errorString());
    return {};
}

} // namespace WorkspaceFiles
