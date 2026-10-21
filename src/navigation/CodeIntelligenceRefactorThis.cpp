// SPDX-License-Identifier: GPL-3.0-only

// CodeIntelligence: Refactor This and Move Class to Its Own File.

#include "CodeIntelligence.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "editor/TextFile.h"
#include "lsp/LanguageServers.h"
#include "lsp/LspClient.h"
#include "refactor/EditPreview.h"
#include "refactor/MoveClass.h"
#include "refactor/RefactoringPreviewPopup.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QMenu>
#include <QPointer>

#include <chrono>
#include <functional>

using namespace LspProtocol;

namespace {

// How long Refactor This waits for the server's refactorings before
// showing Duet's own alone.
constexpr std::chrono::milliseconds refactoringsTimeout{2000};

QList<QJsonObject> usableActions(const QJsonValue &result)
{
    QList<QJsonObject> actions;
    for (const QJsonValue &item : result.toArray()) {
        const QJsonObject action = item.toObject();
        if (!action.contains("disabled") && !action.value("title").toString().isEmpty())
            actions.append(action);
    }
    return actions;
}

// A file the move would create that exists already; empty if none.
QString existingNewFile(const MoveClass::Input &input, const MoveClass::Result &result)
{
    for (const QString &file : result.edit.filePaths()) {
        const bool isNew = file != input.path && file != input.sourcePath;
        if (isNew && QFileInfo::exists(file))
            return file;
    }
    return {};
}

} // namespace

void CodeIntelligence::refactorThis()
{
    EditorWidget *editor = tabs_->currentEditor();
    if (!editor || editor->filePath().isEmpty())
        return;
    LspClient *client = serverFor(editor, true);
    if (!client) {
        chooseRefactoring(editor, nullptr, {});
        return;
    }
    const int from = int(editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONSTART));
    const int to = int(editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONEND));
    const QJsonObject params{
        {"textDocument", QJsonObject{{"uri", uriOf(editor->filePath())}}},
        {"range", toJson(Range{lspPosition(editor, from), lspPosition(editor, to)})},
        {"context", QJsonObject{{"diagnostics", QJsonArray{}}, {"only", QJsonArray{"refactor"}}}},
    };
    client->request(
        QStringLiteral("textDocument/codeAction"), params,
        [this, guard = QPointer<EditorWidget>(editor), server = QPointer<LspClient>(client)](const QJsonValue &result,
                                                                                             const QString &) {
            if (guard)
                chooseRefactoring(guard, server, usableActions(result));
        },
        refactoringsTimeout);
}

void CodeIntelligence::chooseRefactoring(EditorWidget *editor, LspClient *server, const QList<QJsonObject> &actions)
{
    QMenu menu(editor);
    menu.addSection(tr("Refactor This"));
    menu.addAction(tr("Rename…"), this, &CodeIntelligence::rename);
    menu.addAction(tr("Rename File…"), this, [this, path = editor->filePath()] { emit renameFileRequested(path); });
    const bool canMoveClass = editor->languageId() == QLatin1String("cpp") ||
                              editor->languageId() == QLatin1String("c") ||
                              editor->languageId() == QLatin1String("java");
    if (canMoveClass)
        menu.addAction(tr("Move Class to Its Own File"), this, &CodeIntelligence::moveClassToOwnFile);
    if (!actions.isEmpty() && server)
        menu.addSection(QFileInfo(server->program()).fileName());
    for (const QJsonObject &action : actions) {
        menu.addAction(action.value("title").toString(), this,
                       [this, server = QPointer<LspClient>(server), action] { applyRefactoring(server, action); });
    }
    menu.exec(belowCaret(editor));
}

void CodeIntelligence::applyRefactoring(LspClient *server, const QJsonObject &action)
{
    const QString title = action.value("title").toString();
    const QJsonObject command = commandOf(action);
    const auto runCommand = [this, server = QPointer<LspClient>(server), command, title] {
        // The server carries it out and sends back the edit
        // ("workspace/applyEdit").
        if (command.isEmpty() || !server)
            return;
        server->request(
            QStringLiteral("workspace/executeCommand"),
            QJsonObject{{"command", command.value("command")}, {"arguments", command.value("arguments").toArray()}},
            [this, title](const QJsonValue &, const QString &error) {
                if (!error.isEmpty())
                    emit statusMessage(tr("“%1” failed: %2").arg(title, error));
            });
    };
    if (!action.contains("edit")) {
        runCommand();
        return;
    }
    const WorkspaceEdit edit = parseWorkspaceEdit(action.value("edit").toObject());
    showRefactoringPreview(title, edit, [this, title, runCommand](const WorkspaceEdit &kept) {
        applyWorkspaceEdit(kept, title);
        runCommand();
    });
}

void CodeIntelligence::showRefactoringPreview(const QString &title,
                                              const WorkspaceEdit &edit,
                                              const std::function<void(const WorkspaceEdit &)> &apply)
{
    RefactoringPreviewPopup::Preview preview{
        .title = title,
        .root = servers_->folder(),
        .edit = edit,
        .files = EditPreview::simulate(edit, targets()),
    };
    auto *popup = new RefactoringPreviewPopup(std::move(preview), apply, tabs_->window());
    popup->popup();
}

MoveClass::Input CodeIntelligence::moveClassInput(EditorWidget *editor) const
{
    const bool isJava = editor->languageId() == QLatin1String("java");
    MoveClass::Input input{
        .path = editor->filePath(),
        .languageId = isJava ? QStringLiteral("java") : QStringLiteral("cpp"),
        .text = editor->text(),
        .cursor = lspPosition(editor, int(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS))),
    };
    if (isJava)
        return input;
    // The members' definitions, from the header's source.
    input.sourcePath = counterpartOf(input.path);
    if (input.sourcePath.isEmpty())
        return input;
    if (const EditorWidget *source = tabs_->editorFor(input.sourcePath)) {
        input.sourceText = source->text();
        return input;
    }
    QString error;
    const std::optional<DecodedText> decoded = TextFile::load(input.sourcePath, &error);
    input.sourceText = decoded ? decoded->text : QString();
    return input;
}

void CodeIntelligence::moveClassToOwnFile()
{
    EditorWidget *editor = tabs_->currentEditor();
    if (!editor || editor->filePath().isEmpty() || editor->isReadOnly())
        return;
    const MoveClass::Input input = moveClassInput(editor);
    const MoveClass::Result result = MoveClass::plan(input);
    if (!result.error.isEmpty()) {
        emit statusMessage(result.error);
        return;
    }
    if (const QString taken = existingNewFile(input, result); !taken.isEmpty()) {
        emit statusMessage(tr("Cannot move “%1”: %2 already exists").arg(result.className, taken));
        return;
    }
    const QString title = tr("Move “%1” to %2").arg(result.className, QFileInfo(result.newPath).fileName());
    showRefactoringPreview(title, result.edit, [this, title, newPath = result.newPath](const WorkspaceEdit &kept) {
        if (applyWorkspaceEdit(kept, title) > 0)
            tabs_->openFile(newPath);
    });
}
