// SPDX-License-Identifier: GPL-3.0-only

// CodeIntelligence: the server's quick fixes and refactorings (code actions).

#include "CodeIntelligence.h"
#include "diagnostics/ProblemStore.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "lsp/LspClient.h"

#include <QAction>
#include <QJsonArray>
#include <QPointer>

#include <algorithm>

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
    if (!editor || editor->isReadOnly())
        return;
    // Without a server, a build's problem can still be fixed by the agent.
    LspClient *client = serverFor(editor, problemFixFor(editor) != nullptr);
    if (!client) {
        if (problemFixFor(editor))
            chooseQuickFix(editor, nullptr, {});
        return;
    }
    const int from = int(editor->send(SCI_GETSELECTIONSTART));
    const int to = int(editor->send(SCI_GETSELECTIONEND));
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
    QAction *problemFix = problemFixFor(editor);
    if (actions.isEmpty() && !problemFix) {
        emit statusMessage(tr("No quick fixes or refactorings here"));
        return;
    }
    QStringList titles;
    for (const QJsonObject &action : actions)
        titles.append(action.value("title").toString());
    if (problemFix)
        titles.append(QString(problemFix->text()).remove(QLatin1Char('&')));
    const int chosen = chooseFrom(editor, titles);
    if (chosen < 0)
        return;
    if (chosen < actions.size())
        applyCodeAction(server, actions.at(chosen));
    else
        problemFix->trigger();
}

QAction *CodeIntelligence::problemFixFor(const EditorWidget *editor) const
{
    if (!problemFixAction_ || !problemFixAction_->isEnabled() || editor->filePath().isEmpty())
        return nullptr;
    int firstLine = 0;
    int lastLine = 0;
    int index = 0;
    if (editor->hasSelectedText()) {
        editor->getSelection(&firstLine, &index, &lastLine, &index);
    } else {
        editor->getCursorPosition(&firstLine, &index);
        lastLine = firstLine;
    }
    const Problems problems = problems_->problems(editor->filePath());
    const bool hasProblem = std::ranges::any_of(problems, [firstLine, lastLine](const Problem &problem) {
        return problem.line >= firstLine && problem.line <= lastLine;
    });
    return hasProblem ? problemFixAction_ : nullptr;
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
