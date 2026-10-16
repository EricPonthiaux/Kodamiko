#pragma once

#include "completion/Completion.h"

#include <QJsonValue>

class LanguageServers;
class LspClient;
class QJsonArray;

// Completion from the language server of the file's language: members
// after ".", "->" or "::", signatures and types, in the server's order.
class LspCompletionProvider : public ICompletionProvider {
public:
    explicit LspCompletionProvider(LanguageServers *servers);

    bool handles(const CompletionContext &context) const override;
    void complete(const CompletionContext &context, const Callback &done) override;
    QString triggerCharacters(const QString &languageId) const override;

    // Candidates of a "textDocument/completion" result (a list, or an
    // object with "items" and "isIncomplete"). With `client`, the
    // candidates without edits can ask for them once chosen
    // ("completionItem/resolve"). Public for tests.
    static QList<CompletionItem> parse(const QJsonValue &result, bool *incomplete, LspClient *client = nullptr);
    static QList<TextEdit> parseEdits(const QJsonArray &edits);

private:
    LanguageServers *servers_;
};
