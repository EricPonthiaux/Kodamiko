// SPDX-License-Identifier: GPL-3.0-only

#include "EditPreview.h"

#include "EditSimulation.h"
#include "changes/TextPatch.h"

#include <QCoreApplication>
#include <QDir>

#include <algorithm>

using LspProtocol::DocumentChange;
using LspProtocol::DocumentEdit;
using LspProtocol::WorkspaceEdit;

namespace EditPreview {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("EditPreview", text);
}

QString relativePath(const QString &path, const QString &root)
{
    const QString relative = QDir(root).relativeFilePath(path);
    return relative.startsWith(QLatin1String("..")) ? path : relative;
}

QString statusOf(const RefactoredFile &file, const QString &root)
{
    switch (file.kind) {
    case RefactoredFile::Kind::Modified:
        return tr("modified");
    case RefactoredFile::Kind::Created:
        return tr("added");
    case RefactoredFile::Kind::Renamed:
        return tr("renamed from %1").arg(relativePath(file.originalPath, root));
    case RefactoredFile::Kind::Deleted:
        return tr("deleted");
    }
    return {};
}

} // namespace

QList<RefactoredFile> simulate(const WorkspaceEdit &edit, const EditTargets &targets)
{
    EditSimulation simulation(targets);
    for (const DocumentChange &change : edit.changes)
        simulation.apply(change);
    return simulation.files();
}

QList<DiffFile> diffFiles(const QList<RefactoredFile> &changes, const QString &root)
{
    QList<DiffFile> files;
    files.reserve(changes.size());
    for (const RefactoredFile &change : changes) {
        const QByteArray after = change.after.toUtf8();
        files.append({.path = relativePath(change.path, root),
                      .status = statusOf(change, root),
                      .patch = TextPatch::between(change.before.toUtf8(), after),
                      .newText = after});
    }
    return files;
}

WorkspaceEdit without(const WorkspaceEdit &edit, const QSet<QString> &excluded)
{
    WorkspaceEdit kept = edit;
    kept.changes.removeIf([&excluded](const DocumentChange &change) {
        const auto *documentEdit = std::get_if<DocumentEdit>(&change);
        return documentEdit && excluded.contains(documentEdit->filePath);
    });
    return kept;
}

WorkspaceEdit merge(const WorkspaceEdit &code, const WorkspaceEdit &textual)
{
    WorkspaceEdit merged = code;
    QList<DocumentChange> first;
    for (const DocumentChange &change : textual.changes) {
        const auto *addition = std::get_if<DocumentEdit>(&change);
        if (!addition)
            continue;
        const auto joined = std::ranges::find_if(merged.changes, [addition](const DocumentChange &existing) {
            const auto *documentEdit = std::get_if<DocumentEdit>(&existing);
            return documentEdit && documentEdit->filePath == addition->filePath;
        });
        if (joined != merged.changes.end())
            std::get<DocumentEdit>(*joined).edits.append(addition->edits);
        else
            first.append(*addition);
    }
    merged.changes = first + merged.changes;
    return merged;
}

} // namespace EditPreview
