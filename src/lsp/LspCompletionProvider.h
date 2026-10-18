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

    // Smart completion: the candidates of the expected type, asked from
    // the server's signature help inside a call's arguments.
    bool servesSmart() const override { return true; }

    // The files and packages of "from '|'".
    bool servesModulePaths() const override { return true; }

    // Candidates of a "textDocument/completion" result (a list, or an
    // object with "items" and "isIncomplete"). With `client`, the
    // candidates without edits can ask for them once chosen
    // ("completionItem/resolve"). With `context`, a candidate whose edit
    // starts before the typed prefix replaces from there (tsserver's "?."
    // for ".", a scoped package "@scope/name"). Public for tests.
    static QList<CompletionItem> parse(const QJsonValue &result,
                                       bool *incomplete,
                                       LspClient *client = nullptr,
                                       const CompletionContext *context = nullptr);
    static QList<TextEdit> parseEdits(const QJsonArray &edits);
    // The type of the active parameter in a "textDocument/signatureHelp"
    // result, empty if none. Public for tests.
    static QString activeParameterType(const QJsonValue &help, const QString &languageId);

private:
    void request(LspClient *client,
                 const CompletionContext &context,
                 const QString &expectedType,
                 const Callback &done);

    LanguageServers *servers_;
};
