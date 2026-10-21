// SPDX-License-Identifier: GPL-3.0-only

// CodeIntelligence: Find Usages.

#include "CodeIntelligence.h"
#include "UsageFinder.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "lsp/LanguageServers.h"
#include "lsp/LspClient.h"
#include "syntax/LanguageRegistry.h"

#include <QApplication>
#include <QPointer>
#include <QThreadPool>

using namespace LspProtocol;

void CodeIntelligence::findUsages()
{
    EditorWidget *editor = tabs_->currentEditor();
    if (!editor || editor->filePath().isEmpty())
        return;
    const int caret = int(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    const int start = int(editor->SendScintilla(QsciScintillaBase::SCI_WORDSTARTPOSITION, caret, true));
    const int end = int(editor->SendScintilla(QsciScintillaBase::SCI_WORDENDPOSITION, caret, true));
    const QString word = editor->text(start, end);
    if (word.isEmpty()) {
        emit statusMessage(tr("Place the cursor on a name to find its usages"));
        return;
    }
    findUsagesOf({word, editor->filePath(), lspPosition(editor, start)});
}

void CodeIntelligence::findUsagesOf(const UsageSearch &search)
{
    LspClient *client = usageServerFor(search.path);
    emit statusMessage(tr("Looking for the usages of “%1”…").arg(search.name));
    if (!client) {
        findWordUsages(search);
        return;
    }
    const QJsonObject params{
        {"textDocument", QJsonObject{{"uri", uriOf(search.path)}}},
        {"position", toJson(search.position)},
        {"context", QJsonObject{{"includeDeclaration", true}}},
    };
    client->request(QStringLiteral("textDocument/references"), params,
                    [this, search](const QJsonValue &result, const QString &error) {
                        if (!error.isEmpty()) {
                            emit statusMessage(tr("Find Usages failed: %1").arg(error));
                            return;
                        }
                        classifyUsages(search, parseLocations(result));
                    });
}

LspClient *CodeIntelligence::usageServerFor(const QString &path)
{
    if (EditorWidget *editor = tabs_->editorFor(path)) {
        LspClient *client = serverFor(editor, true);
        if (client)
            syncOpenEditors(client, editor->languageId());
        return client;
    }
    // Closed (a search run again): the server reads it from disk.
    return servers_->clientFor(LanguageRegistry::languageForFile(path));
}

void CodeIntelligence::classifyUsages(const UsageSearch &search, const QList<Location> &locations)
{
    QThreadPool::globalInstance()->start(
        [guard = QPointer<CodeIntelligence>(this), search, locations, unsaved = tabs_->unsavedTexts()] {
            UsageResults results{search, UsageFinder::classify(locations, unsaved), true};
            QMetaObject::invokeMethod(
                qApp,
                [guard, results = std::move(results)] {
                    if (guard)
                        guard->reportUsages(results);
                },
                Qt::QueuedConnection);
        });
}

void CodeIntelligence::findWordUsages(const UsageSearch &search)
{
    QThreadPool::globalInstance()->start(
        [guard = QPointer<CodeIntelligence>(this), search, index = index_, unsaved = tabs_->unsavedTexts()] {
            UsageResults results{search, UsageFinder::findWord(search.name, index, unsaved), false};
            QMetaObject::invokeMethod(
                qApp,
                [guard, results = std::move(results)] {
                    if (guard)
                        guard->reportUsages(results);
                },
                Qt::QueuedConnection);
        });
}

void CodeIntelligence::reportUsages(const UsageResults &results)
{
    const QString &name = results.search.name;
    const auto count = int(results.usages.size());
    if (count == 0)
        emit statusMessage(tr("No usages of “%1”").arg(name));
    else if (results.isSemantic)
        emit statusMessage(tr("%n usage(s) of “%1”", nullptr, count).arg(name));
    else
        emit statusMessage(tr("No language server: %n occurrence(s) of the word “%1”", nullptr, count).arg(name));
    emit usagesFound(results);
}
