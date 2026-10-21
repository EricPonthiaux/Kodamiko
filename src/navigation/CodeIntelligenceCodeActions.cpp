// SPDX-License-Identifier: GPL-3.0-only

// CodeIntelligence: the server's quick fixes and refactorings (code actions).

#include "CodeIntelligence.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "lsp/LspClient.h"
#include "refactor/EditPreview.h"
#include "refactor/RefactoringPreviewPopup.h"

#include <QJsonArray>
#include <QPointer>

using namespace LspProtocol;

namespace {

QList<QJsonObject> usableActions(const QJsonArray &codeActions)
{
    QList<QJsonObject> actions;
    for (const QJsonValue &item : codeActions) {
        const QJsonObject action = item.toObject();
        if (!action.contains("disabled") && !action.value("title").toString().isEmpty())
            actions.append(action);
    }
    return actions;
}

// "refactor", "refactor.extract"...: shown in the preview before it is made.
bool isRefactoring(const QJsonObject &action)
{
    return action.value("kind").toString().startsWith(QLatin1String("refactor"));
}

} // namespace

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
                            emit statusMessage(tr("Quick fixes and refactorings failed: %1").arg(error));
                            return;
                        }
                        chooseQuickFix(guard, server, result.toArray());
                    });
}

void CodeIntelligence::chooseQuickFix(EditorWidget *editor, LspClient *server, const QJsonArray &codeActions)
{
    const QList<QJsonObject> actions = usableActions(codeActions);
    if (actions.isEmpty()) {
        emit statusMessage(tr("No quick fixes or refactorings here"));
        return;
    }
    QStringList titles;
    for (const QJsonObject &action : actions)
        titles.append(action.value("title").toString());
    const int chosen = chooseFrom(editor, titles);
    if (chosen >= 0)
        applyCodeAction(server, actions.at(chosen));
}

void CodeIntelligence::applyCodeAction(LspClient *server, const QJsonObject &action)
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
    if (!isRefactoring(action)) {
        applyWorkspaceEdit(edit, title);
        runCommand();
        return;
    }
    review({title, edit, {}, {}}, [runCommand](const RefactoringOutcome &outcome) {
        if (outcome.status == RefactoringOutcome::Status::Applied)
            runCommand();
    });
}
