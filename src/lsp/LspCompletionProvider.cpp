#include "LspCompletionProvider.h"

#include "LanguageServers.h"
#include "LspClient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QUrl>

#include <algorithm>

namespace {

// Above the local providers (2 to 6): the server knows the types.
constexpr int basePriority = 10;

bool isIdentifier(const QString &text)
{
    if (text.isEmpty())
        return false;
    for (const QChar c : text) {
        if (!c.isLetterOrNumber() && c != u'_' && c != u'$')
            return false;
    }
    return true;
}

CompletionItem::Kind kindOf(int lspKind)
{
    using Kind = CompletionItem::Kind;
    switch (lspKind) {
    case 2: return Kind::Method;
    case 3:
    case 4: return Kind::Function; // function, constructor
    case 5: return Kind::Field;
    case 6: return Kind::Variable;
    case 7: return Kind::Class;
    case 8: return Kind::Interface;
    case 9: return Kind::Module;
    case 10: return Kind::Property;
    case 13: return Kind::Enum;
    case 14: return Kind::Keyword;
    case 15: return Kind::Snippet;
    case 20:
    case 21: return Kind::Constant; // enum member, constant
    case 22: return Kind::Struct;
    case 25: return Kind::Type; // type parameter
    }
    return Kind::Text;
}

} // namespace

LspCompletionProvider::LspCompletionProvider(LanguageServers *servers)
    : servers_(servers)
{
}

bool LspCompletionProvider::handles(const CompletionContext &context) const
{
    return servers_->isEnabled() && !context.filePath.isEmpty() && servers_->specFor(context.languageId);
}

QString LspCompletionProvider::triggerCharacters(const QString &languageId) const
{
    const std::optional<LanguageServerSpec> spec = servers_->isEnabled() ? servers_->specFor(languageId) : std::nullopt;
    return spec ? spec->triggerCharacters : QString();
}

QList<TextEdit> LspCompletionProvider::parseEdits(const QJsonArray &edits)
{
    QList<TextEdit> result;
    for (const QJsonValue &value : edits) {
        const QJsonObject edit = value.toObject();
        const QJsonObject range = edit.value("range").toObject();
        const QJsonObject start = range.value("start").toObject();
        const QJsonObject end = range.value("end").toObject();
        result.append({start.value("line").toInt(), start.value("character").toInt(), end.value("line").toInt(),
                       end.value("character").toInt(), edit.value("newText").toString()});
    }
    return result;
}

QList<CompletionItem> LspCompletionProvider::parse(const QJsonValue &result, bool *incomplete, LspClient *client)
{
    // Only a server that resolves items is asked to.
    const bool resolves =
        client && client->capabilities().value("completionProvider").toObject().value("resolveProvider").toBool();
    QPointer<LspClient> server(client);
    QJsonArray list;
    *incomplete = false;
    if (result.isArray()) {
        list = result.toArray();
    } else {
        const QJsonObject object = result.toObject();
        list = object.value("items").toArray();
        *incomplete = object.value("isIncomplete").toBool();
    }

    // The server's order (sortText, else as sent) becomes a priority.
    struct Ranked {
        QString sortText;
        int index;
    };
    std::vector<Ranked> order;
    order.reserve(list.size());
    for (int i = 0; i < list.size(); ++i)
        order.push_back({list[i].toObject().value("sortText").toString(), i});
    std::stable_sort(order.begin(), order.end(), [](const Ranked &a, const Ranked &b) {
        return a.sortText < b.sortText;
    });

    QList<CompletionItem> items;
    items.reserve(list.size());
    for (int rank = 0; rank < int(order.size()); ++rank) {
        const QJsonObject lsp = list[order[rank].index].toObject();
        // clangd prefixes its labels with a space or a "•" (header to insert).
        QString label = lsp.value("label").toString().trimmed();
        if (label.startsWith(QChar(0x2022)))
            label = label.mid(1).trimmed();
        const QString edit = lsp.value("textEdit").toObject().value("newText").toString();
        const QString insert = !edit.isEmpty() ? edit : lsp.value("insertText").toString();
        // The name to match and show: the first of what is inserted, the
        // filter text and the label that is a plain identifier (jdtls
        // filters on "equals(Object obj) : boolean"); else the label's
        // leading identifier.
        QString name;
        for (const QString &candidate : {insert, lsp.value("filterText").toString(), label}) {
            if (isIdentifier(candidate)) {
                name = candidate;
                break;
            }
        }
        if (name.isEmpty()) {
            qsizetype end = 0;
            while (end < label.size() && (label[end].isLetterOrNumber() || label[end] == u'_' || label[end] == u'$'))
                ++end;
            name = end > 0 ? label.left(end) : label;
        }

        // Signature and type, at the right: "(int value)  void".
        QStringList details;
        const QJsonObject labelDetails = lsp.value("labelDetails").toObject();
        QString signature = labelDetails.value("detail").toString();
        if (signature.isEmpty() && label.startsWith(name) && label.size() > name.size())
            signature = label.mid(name.size()); // clangd: "printValue(int value)"
        if (!signature.trimmed().isEmpty())
            details.append(signature.trimmed());
        // The short form first: jdtls's detail is "Main.count : int".
        QString type = labelDetails.value("description").toString().trimmed();
        if (type.isEmpty())
            type = lsp.value("detail").toString().trimmed();
        if (!type.isEmpty() && type != signature.trimmed())
            details.append(type);

        CompletionItem item;
        item.label = name;
        item.insertText = insert.isEmpty() || insert == name ? QString() : insert;
        item.isSnippet = lsp.value("insertTextFormat").toInt() == 2 && !item.insertText.isEmpty();
        item.order = rank;
        item.additionalEdits = parseEdits(lsp.value("additionalTextEdits").toArray());
        if (resolves && item.additionalEdits.isEmpty()) {
            item.resolveEdits = [server, lsp](const CompletionItem::EditsCallback &done) {
                if (!server) {
                    done({});
                    return;
                }
                server->request(QStringLiteral("completionItem/resolve"), lsp,
                                [done](const QJsonValue &resolved, const QString &) {
                                    done(parseEdits(resolved.toObject().value("additionalTextEdits").toArray()));
                                });
            };
        }
        item.detail = details.join(QStringLiteral("  "));
        item.kind = kindOf(lsp.value("kind").toInt());
        // The head of the server's list ranks higher.
        const int count = int(order.size());
        item.priority = basePriority + (rank * 10 < count ? 6 : rank * 3 < count ? 3 : 0);
        items.append(item);
    }
    return items;
}

void LspCompletionProvider::complete(const CompletionContext &context, const Callback &done)
{
    LspClient *client = servers_->clientFor(context.languageId);
    if (!client) {
        done({}, false);
        return;
    }
    servers_->syncDocument(client, context.filePath, context.languageId, context.text);

    QJsonObject completionContext{{"triggerKind", context.triggerCharacter.isNull() ? 1 : 2}};
    if (!context.triggerCharacter.isNull())
        completionContext.insert("triggerCharacter", QString(context.triggerCharacter));
    const QJsonObject params{
        {"textDocument", QJsonObject{{"uri", QUrl::fromLocalFile(context.filePath).toString()}}},
        {"position", QJsonObject{{"line", context.line}, {"character", context.column}}},
        {"context", completionContext},
    };
    client->request(QStringLiteral("textDocument/completion"), params,
                    [done, server = QPointer<LspClient>(client)](const QJsonValue &result, const QString &error) {
                        if (!error.isEmpty()) {
                            done({}, false);
                            return;
                        }
                        bool incomplete = false;
                        const QList<CompletionItem> items = parse(result, &incomplete, server);
                        done(items, incomplete);
                    });
}
