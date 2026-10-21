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
    emit statusMessage(tr("Looking for the usages of “%1”…").arg(search.name));
    collectUsages(search, [guard = QPointer<CodeIntelligence>(this)](const UsageResults &results) {
        if (guard)
            guard->reportUsages(results);
    });
}

void CodeIntelligence::collectUsages(const UsageSearch &search, const UsagesDone &done)
{
    LspClient *client = usageServerFor(search.path);
    if (!client) {
        findWordUsages(search, done);
        return;
    }
    const QJsonObject params{
        {"textDocument", QJsonObject{{"uri", uriOf(search.path)}}},
        {"position", toJson(search.position)},
        {"context", QJsonObject{{"includeDeclaration", true}}},
    };
    client->request(QStringLiteral("textDocument/references"), params,
                    [this, search, done](const QJsonValue &result, const QString &error) {
                        if (!error.isEmpty()) {
                            emit statusMessage(tr("Find Usages failed: %1").arg(error));
                            done({search, {}, true});
                            return;
                        }
                        classifyUsages(search, parseLocations(result), done);
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

void CodeIntelligence::classifyUsages(const UsageSearch &search,
                                      const QList<Location> &locations,
                                      const UsagesDone &done)
{
    QThreadPool::globalInstance()->start([search, locations, unsaved = tabs_->unsavedTexts(), done] {
        UsageResults results{search, UsageFinder::classify(locations, unsaved), true};
        QMetaObject::invokeMethod(qApp, [done, results = std::move(results)] { done(results); }, Qt::QueuedConnection);
    });
}

void CodeIntelligence::findWordUsages(const UsageSearch &search, const UsagesDone &done)
{
    QThreadPool::globalInstance()->start([search, index = index_, unsaved = tabs_->unsavedTexts(), done] {
        UsageResults results{search, UsageFinder::findWord(search.name, index, unsaved), false};
        QMetaObject::invokeMethod(qApp, [done, results = std::move(results)] { done(results); }, Qt::QueuedConnection);
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
