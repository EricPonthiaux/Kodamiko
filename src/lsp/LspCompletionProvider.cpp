// SPDX-License-Identifier: GPL-3.0-only

#include "LspCompletionProvider.h"

#include "LanguageServers.h"
#include "LspClient.h"
#include "completion/CodeScan.h"
#include "completion/ExpectedType.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QUrl>

#include <algorithm>

namespace {

// Above the local providers (2 to 6): the server knows the types.
constexpr int basePriority = 10;

bool isIdentifierChar(QChar c)
{
    return c.isLetterOrNumber() || c == u'_' || c == u'$';
}

bool isIdentifier(const QString &text)
{
    return !text.isEmpty() && std::ranges::all_of(text, isIdentifierChar);
}

// A string, or MarkupContent ({"kind", "value"}).
QString documentationOf(const QJsonValue &documentation)
{
    return (documentation.isObject() ? documentation.toObject().value("value").toString() : documentation.toString())
        .trimmed();
}

CompletionItem::Kind kindOf(int lspKind)
{
    using Kind = CompletionItem::Kind;
    switch (lspKind) {
    case 2:
        return Kind::Method;
    case 3:
    case 4:
        return Kind::Function; // function, constructor
    case 5:
        return Kind::Field;
    case 6:
        return Kind::Variable;
    case 7:
        return Kind::Class;
    case 8:
        return Kind::Interface;
    case 9:
        return Kind::Module;
    case 10:
        return Kind::Property;
    case 13:
        return Kind::Enum;
    case 14:
        return Kind::Keyword;
    case 15:
        return Kind::Snippet;
    case 20:
    case 21:
        return Kind::Constant; // enum member, constant
    case 22:
        return Kind::Struct;
    case 25:
        return Kind::Type; // type parameter
    default:
        return Kind::Text;
    }
}

// Where a candidate's edit starts, in bytes of the context's text, when on
// the cursor's line before the typed prefix; else -1.
int editStart(const QJsonObject &edit, const CompletionContext &context)
{
    // A range, or an insert-replace edit's "insert" range.
    const QJsonObject range = edit.contains("range") ? edit.value("range").toObject() : edit.value("insert").toObject();
    const QJsonObject start = range.value("start").toObject();
    if (start.isEmpty() || start.value("line").toInt() != context.line)
        return -1;
    const int column = start.value("character").toInt();
    if (column >= context.column)
        return -1;
    const int lineStart = int(context.text.lastIndexOf('\n', context.position - 1)) + 1;
    const int at =
        lineStart +
        int(QString::fromUtf8(context.text.mid(lineStart, context.position - lineStart)).left(column).toUtf8().size());
    return at < context.prefixStart ? at : -1;
}

// The indexes of the server's candidates in its order: by sortText, else
// as sent.
std::vector<int> serverOrder(const QJsonArray &list)
{
    std::vector<int> order(list.size());
    for (int i = 0; i < int(list.size()); ++i)
        order[i] = i;
    const auto sortText = [&list](int index) { return list[index].toObject().value("sortText").toString(); };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return sortText(a) < sortText(b); });
    return order;
}

// clangd prefixes its labels with a space or a "•" (header to insert).
QString labelOf(const QJsonObject &lsp)
{
    const QString label = lsp.value("label").toString().trimmed();
    return label.startsWith(QChar(0x2022)) ? label.mid(1).trimmed() : label;
}

// The name to match and show: the first of what is inserted, the filter
// text and the label that is a plain identifier (jdtls filters on
// "equals(Object obj) : boolean"); else the label's leading identifier.
QString nameOf(const QString &insert, const QString &filterText, const QString &label)
{
    for (const QString &candidate : {insert, filterText, label}) {
        if (isIdentifier(candidate))
            return candidate;
    }
    const qsizetype length = std::ranges::find_if_not(label, isIdentifierChar) - label.begin();
    return length > 0 ? label.left(length) : label;
}

// Signature and type, at the right: "(int value)  void".
QString detailOf(const QJsonObject &lsp, const QString &label, const QString &name)
{
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
    return details.join(QStringLiteral("  "));
}

QString typeOf(const QJsonObject &lsp, const QString &label)
{
    const QString description = lsp.value("labelDetails").toObject().value("description").toString();
    QString type =
        ExpectedType::ofDetail(!description.trimmed().isEmpty() ? description : lsp.value("detail").toString());
    if (type.isEmpty() && label.contains(QLatin1String(" : ")))
        return ExpectedType::ofDetail(label); // jdtls: "size() : int"
    return type;
}

// The candidate `lsp`, at `rank` of the server's `count`.
CompletionItem itemOf(const QJsonObject &lsp, int rank, int count)
{
    const QString label = labelOf(lsp);
    const QString edit = lsp.value("textEdit").toObject().value("newText").toString();
    const QString insert = !edit.isEmpty() ? edit : lsp.value("insertText").toString();
    const QString name = nameOf(insert, lsp.value("filterText").toString(), label);

    CompletionItem item;
    item.label = name;
    item.insertText = insert.isEmpty() || insert == name ? QString() : insert;
    item.isSnippet = lsp.value("insertTextFormat").toInt() == 2 && !item.insertText.isEmpty();
    item.order = rank;
    item.additionalEdits = LspCompletionProvider::parseEdits(lsp.value("additionalTextEdits").toArray());
    item.detail = detailOf(lsp, label, name);
    item.type = typeOf(lsp, label);
    item.kind = kindOf(lsp.value("kind").toInt());
    // The server ranks by the expected type and the scope: its order
    // weighs in, from +8 for its first candidate down to +0, on top of
    // how well the typed letters match.
    item.priority = basePriority + 8 - (rank * 8) / std::max(count, 1);
    item.documentation = documentationOf(lsp.value("documentation"));
    return item;
}

// What the item lacks (edits, documentation), asked from `server` once
// the item is chosen.
void resolveLater(CompletionItem &item, const QPointer<LspClient> &server, const QJsonObject &lsp)
{
    if (item.additionalEdits.isEmpty()) {
        item.resolveEdits = [server, lsp](const CompletionItem::EditsCallback &done) {
            if (!server) {
                done({});
                return;
            }
            server->request(
                QStringLiteral("completionItem/resolve"), lsp, [done](const QJsonValue &resolved, const QString &) {
                    done(LspCompletionProvider::parseEdits(resolved.toObject().value("additionalTextEdits").toArray()));
                });
        };
    }
    if (item.documentation.isEmpty()) {
        item.resolveDocumentation = [server, lsp](const CompletionItem::TextCallback &done) {
            if (!server) {
                done({});
                return;
            }
            server->request(QStringLiteral("completionItem/resolve"), lsp,
                            [done](const QJsonValue &resolved, const QString &) {
                                done(documentationOf(resolved.toObject().value("documentation")));
                            });
        };
    }
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

QList<CompletionItem> LspCompletionProvider::parse(const QJsonValue &result,
                                                   bool *incomplete,
                                                   LspClient *client,
                                                   const CompletionContext *context)
{
    // Only a server that resolves items is asked to.
    const bool resolves =
        client && client->capabilities().value("completionProvider").toObject().value("resolveProvider").toBool();
    const QPointer<LspClient> server(client);
    QJsonArray list;
    *incomplete = false;
    if (result.isArray()) {
        list = result.toArray();
    } else {
        const QJsonObject object = result.toObject();
        list = object.value("items").toArray();
        *incomplete = object.value("isIncomplete").toBool();
    }

    // The server's order becomes a priority.
    const std::vector<int> order = serverOrder(list);
    const int count = int(order.size());
    QList<CompletionItem> items;
    items.reserve(list.size());
    for (int rank = 0; rank < count; ++rank) {
        const QJsonObject lsp = list[order[rank]].toObject();
        CompletionItem item = itemOf(lsp, rank, count);
        if (context)
            item.replaceStart = editStart(lsp.value("textEdit").toObject(), *context);
        if (resolves)
            resolveLater(item, server, lsp);
        items.append(item);
    }
    return items;
}

QString LspCompletionProvider::activeParameterType(const QJsonValue &result, const QString &languageId)
{
    const QJsonObject help = result.toObject();
    const QJsonArray signatures = help.value("signatures").toArray();
    if (signatures.isEmpty())
        return {};
    const int active = std::clamp(help.value("activeSignature").toInt(), 0, int(signatures.size()) - 1);
    const QJsonObject signature = signatures.at(active).toObject();
    const int parameter = signature.contains("activeParameter") ? signature.value("activeParameter").toInt()
                                                                : help.value("activeParameter").toInt();
    const QJsonArray parameters = signature.value("parameters").toArray();
    if (parameter < 0 || parameter >= parameters.size())
        return {};
    // Its label: offsets in the signature's, or its text.
    const QJsonValue name = parameters.at(parameter).toObject().value("label");
    QString label = name.toString();
    if (name.isArray()) {
        const QString text = signature.value("label").toString();
        const int start = name.toArray().at(0).toInt();
        label = text.mid(start, name.toArray().at(1).toInt() - start);
    }
    return ExpectedType::ofParameter(label, languageId);
}

void LspCompletionProvider::complete(const CompletionContext &context, const Callback &done)
{
    LspClient *client = servers_->clientFor(context.languageId);
    if (!client) {
        done({}, false);
        return;
    }
    servers_->syncDocument(client, context.filePath, context.languageId, context.text);
    if (context.mode != CompletionContext::Mode::Smart || !context.expectedType.isEmpty() ||
        CodeScan::enclosingParenthesis(CodeScan::blank(context.text.left(context.prefixStart)), context.prefixStart) <
            0) {
        request(client, context, context.expectedType, done);
        return;
    }
    // In a call's arguments: the parameter's type, from the server.
    const QJsonObject params{
        {"textDocument", QJsonObject{{"uri", QUrl::fromLocalFile(context.filePath).toString()}}},
        {"position", QJsonObject{{"line", context.line}, {"character", context.column}}},
    };
    client->request(
        QStringLiteral("textDocument/signatureHelp"), params,
        [this, context, done, server = QPointer<LspClient>(client)](const QJsonValue &result, const QString &) {
            if (!server) {
                done({}, false);
                return;
            }
            request(server, context, activeParameterType(result, context.languageId), done);
        });
}

void LspCompletionProvider::request(LspClient *client,
                                    const CompletionContext &context,
                                    const QString &expectedType,
                                    const Callback &done)
{
    QJsonObject completionContext{{"triggerKind", context.triggerCharacter.isNull() ? 1 : 2}};
    if (!context.triggerCharacter.isNull())
        completionContext.insert("triggerCharacter", QString(context.triggerCharacter));
    const QJsonObject params{
        {"textDocument", QJsonObject{{"uri", QUrl::fromLocalFile(context.filePath).toString()}}},
        {"position", QJsonObject{{"line", context.line}, {"character", context.column}}},
        {"context", completionContext},
    };
    const bool smart = context.mode == CompletionContext::Mode::Smart;
    // "new |": the class to create.
    int before = context.prefixStart;
    while (before > 0 && context.text[before - 1] == ' ')
        --before;
    const bool afterNew = before >= 3 && context.text.mid(before - 3, 3) == "new" &&
                          (before == 3 || !CodeScan::isIdentifierByte(context.text[before - 4]));
    client->request(QStringLiteral("textDocument/completion"), params,
                    [done, smart, expectedType, afterNew, context,
                     server = QPointer<LspClient>(client)](const QJsonValue &result, const QString &error) {
                        if (!error.isEmpty()) {
                            done({}, false);
                            return;
                        }
                        bool incomplete = false;
                        QList<CompletionItem> items = parse(result, &incomplete, server, &context);
                        if (smart) {
                            items.removeIf([&](const CompletionItem &item) {
                                return !ExpectedType::accepts(expectedType, item, afterNew);
                            });
                        }
                        done(items, incomplete);
                    });
}
