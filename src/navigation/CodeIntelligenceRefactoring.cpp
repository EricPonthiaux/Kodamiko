// SPDX-License-Identifier: GPL-3.0-only

// CodeIntelligence: rename, quick fixes, and the refactorings' edits.

#include "CodeIntelligence.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "lsp/LanguageServers.h"
#include "lsp/LspClient.h"
#include "refactor/EditPreview.h"
#include "refactor/RefactoringHistory.h"
#include "refactor/RefactoringPreviewPopup.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QPointer>

#include <chrono>

using namespace LspProtocol;

namespace {

constexpr std::chrono::seconds prepareRenameTimeout{10};

// A Command, or a CodeAction carrying one; empty if there is none.
QJsonObject commandOf(const QJsonObject &action)
{
    const QJsonValue command = action.value("command");
    if (command.isObject())
        return command.toObject();
    if (command.isString())
        return action;
    return {};
}

} // namespace

void CodeIntelligence::rename()
{
    EditorWidget *editor = tabs_->currentEditor();
    if (!editor || editor->isReadOnly())
        return;
    const int caret = int(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    const int start = int(editor->SendScintilla(QsciScintillaBase::SCI_WORDSTARTPOSITION, caret, true));
    const int end = int(editor->SendScintilla(QsciScintillaBase::SCI_WORDENDPOSITION, caret, true));
    const QString word = editor->text(start, end);
    if (word.isEmpty()) {
        emit statusMessage(tr("Place the cursor on a name to rename it"));
        return;
    }
    LspClient *client = serverFor(editor);
    if (!client)
        return;
    const QJsonValue renameProvider = client->capabilities().value("renameProvider");
    const bool canPrepare = renameProvider.toObject().value("prepareProvider").toBool();
    if (!canPrepare) {
        showRenamePopup(editor, start, word);
        return;
    }
    // The server says first whether the name can be renamed (not a keyword,
    // not a symbol of the standard library), and how it is spelled.
    client->request(
        QStringLiteral("textDocument/prepareRename"), positionParams(editor, start),
        [this, guard = QPointer<EditorWidget>(editor), start, word](const QJsonValue &result, const QString &error) {
            if (!guard)
                return;
            if (!error.isEmpty()) {
                emit statusMessage(tr("Cannot rename “%1”: %2").arg(word, error));
                return;
            }
            if (result.isNull()) {
                emit statusMessage(tr("“%1” cannot be renamed").arg(word));
                return;
            }
            const QString placeholder = result.toObject().value("placeholder").toString();
            showRenamePopup(guard, start, placeholder.isEmpty() ? word : placeholder);
        },
        prepareRenameTimeout);
}

void CodeIntelligence::showRenamePopup(EditorWidget *editor, int bytePosition, const QString &name)
{
    const QPointer<EditorWidget> target = editor;
    auto *popup = new RenameSymbolPopup(
        name, QFileInfo(editor->filePath()).fileName(),
        [this, target, bytePosition, name](const QString &newName, RenameSymbolPopup::Mode mode) -> QString {
            if (newName.isEmpty())
                return tr("Specify a new name");
            if (target && newName != name)
                renameAt(target, {bytePosition, name, newName, mode});
            return {};
        },
        editor);
    popup->popup();
}

void CodeIntelligence::renameAt(EditorWidget *editor, const Rename &rename)
{
    LspClient *client = serverFor(editor);
    if (!client)
        return;
    syncOpenEditors(client, editor->languageId());
    QJsonObject params = positionParams(editor, rename.bytePosition);
    params.insert("newName", rename.newName);
    client->request(QStringLiteral("textDocument/rename"), params,
                    [this, rename](const QJsonValue &result, const QString &error) {
                        if (!error.isEmpty()) {
                            emit statusMessage(tr("Cannot rename “%1”: %2").arg(rename.name, error));
                            return;
                        }
                        const WorkspaceEdit edit = parseWorkspaceEdit(result.toObject());
                        if (edit.isEmpty()) {
                            emit statusMessage(tr("Nothing to rename here"));
                            return;
                        }
                        if (rename.mode == RenameSymbolPopup::Mode::Preview)
                            showPreview(edit, rename);
                        else
                            completeRename(edit, rename);
                    });
}

void CodeIntelligence::completeRename(const WorkspaceEdit &edit, const Rename &rename)
{
    const int files = applyWorkspaceEdit(edit, tr("Rename “%1” to “%2”").arg(rename.name, rename.newName));
    if (files == 0)
        return; // the errors were said
    emit statusMessage(files == 1 ? tr("Renamed “%1” to “%2”").arg(rename.name, rename.newName)
                                  : tr("Renamed “%1” to “%2” in %3 files").arg(rename.name, rename.newName).arg(files));
}

void CodeIntelligence::showPreview(const WorkspaceEdit &edit, const Rename &rename)
{
    RefactoringPreviewPopup::Preview preview{
        .title = tr("Rename “%1” to “%2”").arg(rename.name, rename.newName),
        .root = servers_->folder(),
        .edit = edit,
        .files = EditPreview::simulate(edit, targets()),
    };
    auto *popup = new RefactoringPreviewPopup(
        std::move(preview), [this, rename](const WorkspaceEdit &kept) { completeRename(kept, rename); },
        tabs_->window());
    popup->popup();
}

void CodeIntelligence::showQuickFixes()
{
    EditorWidget *editor = tabs_->currentEditor();
    LspClient *client = serverFor(editor);
    if (!client || editor->isReadOnly())
        return;
    const int from = int(editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONSTART));
    const int to = int(editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONEND));
    const QJsonObject params{
        {"textDocument", QJsonObject{{"uri", uriOf(editor->filePath())}}},
        {"range", toJson(Range{lspPosition(editor, from), lspPosition(editor, to)})},
        {"context", QJsonObject{{"diagnostics", QJsonArray{}}}},
    };
    client->request(QStringLiteral("textDocument/codeAction"), params,
                    [this, guard = QPointer<EditorWidget>(editor),
                     server = QPointer<LspClient>(client)](const QJsonValue &result, const QString &error) {
                        if (!guard)
                            return;
                        if (!error.isEmpty()) {
                            emit statusMessage(tr("Quick fixes failed: %1").arg(error));
                            return;
                        }
                        chooseQuickFix(guard, server, result.toArray());
                    });
}

void CodeIntelligence::chooseQuickFix(EditorWidget *editor, LspClient *server, const QJsonArray &codeActions)
{
    QList<QJsonObject> actions;
    QStringList titles;
    for (const QJsonValue &item : codeActions) {
        const QJsonObject action = item.toObject();
        if (action.contains("disabled") || action.value("title").toString().isEmpty())
            continue;
        actions.append(action);
        titles.append(action.value("title").toString());
    }
    if (actions.isEmpty()) {
        emit statusMessage(tr("No quick fixes here"));
        return;
    }
    const int chosen = chooseFrom(editor, titles);
    if (chosen < 0)
        return;
    const QJsonObject action = actions.at(chosen);
    if (action.contains("edit"))
        applyWorkspaceEdit(parseWorkspaceEdit(action.value("edit").toObject()), action.value("title").toString());
    // The server carries the command out and sends back the edit
    // ("workspace/applyEdit").
    const QJsonObject command = commandOf(action);
    if (command.isEmpty() || !server)
        return;
    server->request(
        QStringLiteral("workspace/executeCommand"),
        QJsonObject{{"command", command.value("command")}, {"arguments", command.value("arguments").toArray()}},
        [this](const QJsonValue &, const QString &error) {
            if (!error.isEmpty())
                emit statusMessage(tr("The quick fix failed: %1").arg(error));
        });
}

int CodeIntelligence::applyWorkspaceEdit(const WorkspaceEdit &edit, const QString &description)
{
    return applyEdit(edit, description).filesChanged;
}

WorkspaceEditing::Outcome CodeIntelligence::applyEdit(const WorkspaceEdit &edit, const QString &description)
{
    WorkspaceEditing::Outcome outcome = WorkspaceEditing::apply(edit, targets());
    if (!outcome.errors.isEmpty())
        emit statusMessage(outcome.errors.join(QLatin1Char(' ')));
    history_->record(description, outcome.journal);
    return outcome;
}

void CodeIntelligence::undoRefactoring()
{
    const QString description = history_->lastDescription();
    if (description.isEmpty()) {
        emit statusMessage(tr("No refactoring to undo"));
        return;
    }
    const QStringList errors = history_->undoLast(targets());
    emit statusMessage(errors.isEmpty() ? tr("Undid “%1”").arg(description) : errors.join(QLatin1Char(' ')));
}
