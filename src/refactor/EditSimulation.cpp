// SPDX-License-Identifier: GPL-3.0-only

#include "EditSimulation.h"

#include "WorkspaceFiles.h"

#include <QFileInfo>

#include <algorithm>
#include <variant>

using namespace LspProtocol;

EditSimulation::EditSimulation(const EditTargets &targets)
    : targets_(targets)
{
}

void EditSimulation::apply(const DocumentChange &change)
{
    if (const auto *documentEdit = std::get_if<DocumentEdit>(&change)) {
        editText(*documentEdit);
        return;
    }
    const auto &operation = std::get<FileOperation>(change);
    switch (operation.kind) {
    case FileOperation::Kind::Create:
        create(operation.filePath);
        break;
    case FileOperation::Kind::Rename:
        rename(operation);
        break;
    case FileOperation::Kind::Delete:
        remove(operation.filePath);
        break;
    }
}

RefactoredFile *EditSimulation::find(const QString &path)
{
    const auto found = std::ranges::find_if(files_, [&](const RefactoredFile &file) {
        return file.path == path && file.kind != RefactoredFile::Kind::Deleted;
    });
    return found == files_.end() ? nullptr : &*found;
}

RefactoredFile *EditSimulation::load(const QString &path)
{
    if (RefactoredFile *file = find(path))
        return file;
    RefactoredFile file{.kind = RefactoredFile::Kind::Modified, .path = path, .originalPath = path};
    if (!QFileInfo(path).isDir()) {
        const std::optional<DecodedText> document = WorkspaceFiles::documentOf(path, targets_);
        if (!document)
            return nullptr;
        file.before = document->text;
        file.after = document->text;
        file.lineEnding = document->format.lineEnding;
    }
    files_.append(std::move(file));
    return &files_.last();
}

void EditSimulation::editText(const DocumentEdit &edit)
{
    RefactoredFile *file = load(edit.filePath);
    if (!file)
        return;
    QList<TextEdit> edits = edit.edits;
    for (TextEdit &textEdit : edits)
        textEdit.newText = TextFile::withLineEnding(textEdit.newText, file->lineEnding);
    file->after = applyEdits(file->after, edits);
}

void EditSimulation::create(const QString &path)
{
    if (RefactoredFile *existing = find(path)) {
        existing->after.clear(); // overwritten
        existing->hasTextEditsOnly = false;
        return;
    }
    files_.append({.kind = RefactoredFile::Kind::Created, .path = path, .hasTextEditsOnly = false});
}

void EditSimulation::rename(const FileOperation &operation)
{
    RefactoredFile *file = load(operation.filePath);
    if (!file)
        return;
    file->path = operation.newPath;
    if (file->kind != RefactoredFile::Kind::Created)
        file->kind = RefactoredFile::Kind::Renamed;
    file->hasTextEditsOnly = false;
}

void EditSimulation::remove(const QString &path)
{
    RefactoredFile *file = load(path);
    if (!file)
        return;
    file->kind = RefactoredFile::Kind::Deleted;
    file->after.clear();
    file->hasTextEditsOnly = false;
}
