#include "McpSession.h"

#include "McpRegistry.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>

namespace {

// JSON-RPC error codes.
constexpr int parseError = -32700;
constexpr int invalidRequest = -32600;
constexpr int methodNotFound = -32601;
constexpr int invalidParams = -32602;
// MCP: a resource that does not exist.
constexpr int resourceNotFound = -32002;

QJsonObject toolDeclaration(const McpTool &tool)
{
    QJsonObject declaration{{QLatin1String("name"), tool.name},
                            {QLatin1String("description"), tool.description},
                            {QLatin1String("inputSchema"),
                             tool.inputSchema.isEmpty() ? QJsonObject{{QLatin1String("type"), QLatin1String("object")}}
                                                        : tool.inputSchema}};
    if (!tool.title.isEmpty())
        declaration.insert(QLatin1String("title"), tool.title);
    if (!tool.outputSchema.isEmpty())
        declaration.insert(QLatin1String("outputSchema"), tool.outputSchema);
    QJsonObject annotations{{QLatin1String("readOnlyHint"), tool.readOnly},
                            {QLatin1String("destructiveHint"), tool.destructive},
                            {QLatin1String("idempotentHint"), tool.idempotent},
                            // Tools act on this editor and project only.
                            {QLatin1String("openWorldHint"), false}};
    if (!tool.title.isEmpty())
        annotations.insert(QLatin1String("title"), tool.title);
    declaration.insert(QLatin1String("annotations"), annotations);
    return declaration;
}

QJsonObject toolResult(const McpResult &result)
{
    QString text = result.text;
    if (text.isEmpty() && !result.structured.isUndefined() && !result.structured.isNull()) {
        text = QString::fromUtf8(result.structured.isObject()
                                     ? QJsonDocument(result.structured.toObject()).toJson(QJsonDocument::Compact)
                                     : QJsonDocument(result.structured.toArray()).toJson(QJsonDocument::Compact));
    }
    QJsonObject object{{QLatin1String("content"), QJsonArray{QJsonObject{{QLatin1String("type"), QLatin1String("text")},
                                                                         {QLatin1String("text"), text}}}},
                       {QLatin1String("isError"), result.isError}};
    if (result.structured.isObject())
        object.insert(QLatin1String("structuredContent"), result.structured);
    return object;
}

} // namespace

McpSession::McpSession(QLocalSocket *socket, McpRegistry *registry, QObject *parent)
    : QObject(parent)
    , socket_(socket)
    , registry_(registry)
{
    if (socket) {
        socket->setParent(this);
        connect(socket, &QLocalSocket::readyRead, this, &McpSession::readLines);
        connect(socket, &QLocalSocket::disconnected, this, [this] {
            emit closed();
            deleteLater();
        });
    }
    connect(registry_, &McpRegistry::resourceUpdated, this, [this](const QString &uri) {
        if (subscriptions_.contains(uri))
            send(QJsonObject{{QLatin1String("jsonrpc"), QLatin1String("2.0")},
                             {QLatin1String("method"), QLatin1String("notifications/resources/updated")},
                             {QLatin1String("params"), QJsonObject{{QLatin1String("uri"), uri}}}});
    });
    if (socket && socket->canReadLine())
        QMetaObject::invokeMethod(this, &McpSession::readLines, Qt::QueuedConnection);
}

const QStringList &McpSession::protocolVersions()
{
    static const QStringList versions{QStringLiteral("2025-06-18"), QStringLiteral("2025-03-26"),
                                      QStringLiteral("2024-11-05")};
    return versions;
}

void McpSession::readLines()
{
    while (socket_ && socket_->canReadLine()) {
        const QByteArray line = socket_->readLine().trimmed();
        if (line.isEmpty())
            continue;
        QJsonParseError parse;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parse);
        if (!document.isObject()) {
            error(QJsonValue::Null, parseError, parse.errorString());
            continue;
        }
        handleMessage(document.object());
    }
}

void McpSession::send(const QJsonObject &message)
{
    emit sent(message);
    if (socket_ && socket_->state() == QLocalSocket::ConnectedState) {
        socket_->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
        socket_->flush();
    }
}

void McpSession::result(const QJsonValue &id, const QJsonObject &value)
{
    send(QJsonObject{
        {QLatin1String("jsonrpc"), QLatin1String("2.0")}, {QLatin1String("id"), id}, {QLatin1String("result"), value}});
}

void McpSession::error(const QJsonValue &id, int code, const QString &message)
{
    send(QJsonObject{
        {QLatin1String("jsonrpc"), QLatin1String("2.0")},
        {QLatin1String("id"), id},
        {QLatin1String("error"), QJsonObject{{QLatin1String("code"), code}, {QLatin1String("message"), message}}}});
}

void McpSession::handleMessage(const QJsonObject &message)
{
    const QString method = message.value(QLatin1String("method")).toString();
    const QJsonValue id = message.value(QLatin1String("id"));
    const QJsonObject params = message.value(QLatin1String("params")).toObject();
    const bool notification = !message.contains(QLatin1String("id"));
    if (method.isEmpty()) {
        // A response to a request of ours (none are sent today) or garbage.
        if (!notification && !message.contains(QLatin1String("result")) && !message.contains(QLatin1String("error")))
            error(id, invalidRequest, QStringLiteral("Not a JSON-RPC request"));
        return;
    }
    if (notification) {
        if (method == QLatin1String("notifications/initialized"))
            initialized_ = true;
        return; // cancellations and the like need no answer
    }

    if (method == QLatin1String("initialize")) {
        const QString requested = params.value(QLatin1String("protocolVersion")).toString();
        const QString version = protocolVersions().contains(requested) ? requested : protocolVersions().first();
        QJsonObject answer{
            {QLatin1String("protocolVersion"), version},
            {QLatin1String("capabilities"),
             QJsonObject{{QLatin1String("tools"), QJsonObject{{QLatin1String("listChanged"), false}}},
                         {QLatin1String("resources"),
                          QJsonObject{{QLatin1String("subscribe"), true}, {QLatin1String("listChanged"), false}}},
                         {QLatin1String("prompts"), QJsonObject{{QLatin1String("listChanged"), false}}},
                         {QLatin1String("logging"), QJsonObject{}}}},
            {QLatin1String("serverInfo"),
             QJsonObject{{QLatin1String("name"), QLatin1String("duet")},
                         {QLatin1String("title"), QLatin1String("Duet")},
                         {QLatin1String("version"), QCoreApplication::applicationVersion()}}},
        };
        if (!registry_->instructions.isEmpty())
            answer.insert(QLatin1String("instructions"), registry_->instructions);
        result(id, answer);
    } else if (method == QLatin1String("ping")) {
        result(id, {});
    } else if (method == QLatin1String("tools/list")) {
        QJsonArray tools;
        for (const McpTool &tool : registry_->tools())
            tools.append(toolDeclaration(tool));
        result(id, QJsonObject{{QLatin1String("tools"), tools}});
    } else if (method == QLatin1String("tools/call")) {
        callTool(id, params);
    } else if (method == QLatin1String("resources/list")) {
        QJsonArray resources;
        for (const McpResource &resource : registry_->resources()) {
            if (resource.isTemplate)
                continue;
            resources.append(QJsonObject{{QLatin1String("uri"), resource.uri},
                                         {QLatin1String("name"), resource.name},
                                         {QLatin1String("title"), resource.title},
                                         {QLatin1String("description"), resource.description},
                                         {QLatin1String("mimeType"), resource.mimeType}});
        }
        result(id, QJsonObject{{QLatin1String("resources"), resources}});
    } else if (method == QLatin1String("resources/templates/list")) {
        QJsonArray templates;
        for (const McpResource &resource : registry_->resources()) {
            if (!resource.isTemplate)
                continue;
            templates.append(QJsonObject{{QLatin1String("uriTemplate"), resource.uri},
                                         {QLatin1String("name"), resource.name},
                                         {QLatin1String("title"), resource.title},
                                         {QLatin1String("description"), resource.description},
                                         {QLatin1String("mimeType"), resource.mimeType}});
        }
        result(id, QJsonObject{{QLatin1String("resourceTemplates"), templates}});
    } else if (method == QLatin1String("resources/read")) {
        readResource(id, params);
    } else if (method == QLatin1String("resources/subscribe")) {
        subscriptions_.insert(params.value(QLatin1String("uri")).toString());
        result(id, {});
    } else if (method == QLatin1String("resources/unsubscribe")) {
        subscriptions_.remove(params.value(QLatin1String("uri")).toString());
        result(id, {});
    } else if (method == QLatin1String("prompts/list")) {
        QJsonArray prompts;
        for (const McpPrompt &prompt : registry_->prompts()) {
            QJsonArray arguments;
            for (const McpPromptArgument &argument : prompt.arguments)
                arguments.append(QJsonObject{{QLatin1String("name"), argument.name},
                                             {QLatin1String("description"), argument.description},
                                             {QLatin1String("required"), argument.required}});
            prompts.append(QJsonObject{{QLatin1String("name"), prompt.name},
                                       {QLatin1String("title"), prompt.title},
                                       {QLatin1String("description"), prompt.description},
                                       {QLatin1String("arguments"), arguments}});
        }
        result(id, QJsonObject{{QLatin1String("prompts"), prompts}});
    } else if (method == QLatin1String("prompts/get")) {
        getPrompt(id, params);
    } else if (method == QLatin1String("logging/setLevel")) {
        result(id, {});
    } else {
        error(id, methodNotFound, QStringLiteral("Unknown method %1").arg(method));
    }
}

void McpSession::callTool(const QJsonValue &id, const QJsonObject &params)
{
    const QString name = params.value(QLatin1String("name")).toString();
    const McpTool *tool = registry_->tool(name);
    if (!tool) {
        error(id, invalidParams, QStringLiteral("Unknown tool %1").arg(name));
        return;
    }
    const QJsonObject arguments = params.value(QLatin1String("arguments")).toObject();
    const QString problem = McpRegistry::validate(tool->inputSchema, arguments);
    if (!problem.isEmpty()) {
        error(id, invalidParams, problem);
        return;
    }
    const QPointer<McpSession> self(this);
    const McpReply reply = [self, id](const McpResult &outcome) {
        if (self)
            self->result(id, toolResult(outcome));
    };
    McpProgress progress = [](double, double, const QString &) { };
    const QJsonValue token = params.value(QLatin1String("_meta")).toObject().value(QLatin1String("progressToken"));
    if (!token.isUndefined() && !token.isNull()) {
        progress = [self, token](double done, double total, const QString &message) {
            if (!self)
                return;
            QJsonObject notice{{QLatin1String("progressToken"), token}, {QLatin1String("progress"), done}};
            if (total > 0)
                notice.insert(QLatin1String("total"), total);
            if (!message.isEmpty())
                notice.insert(QLatin1String("message"), message);
            self->send(QJsonObject{{QLatin1String("jsonrpc"), QLatin1String("2.0")},
                                   {QLatin1String("method"), QLatin1String("notifications/progress")},
                                   {QLatin1String("params"), notice}});
        };
    }
    // Copies: the registry's tool may be gone by the time the user answers.
    const auto run = tool->run;
    if (!tool->confirm) {
        run(arguments, reply, progress);
        return;
    }
    const QString what = tool->describe ? tool->describe(arguments) : tool->title;
    registry_->confirm(tool->title.isEmpty() ? name : tool->title, what,
                       [run, arguments, reply, progress](bool allowed) {
                           if (allowed)
                               run(arguments, reply, progress);
                           else
                               reply(McpResult::error(QStringLiteral("The user refused this action in Duet.")));
                       });
}

void McpSession::readResource(const QJsonValue &id, const QJsonObject &params)
{
    const QString uri = params.value(QLatin1String("uri")).toString();
    const McpResource *resource = registry_->resourceFor(uri);
    const std::optional<QString> content = resource && resource->read ? resource->read(uri) : std::nullopt;
    if (!content) {
        error(id, resourceNotFound, QStringLiteral("No resource %1").arg(uri));
        return;
    }
    result(id, QJsonObject{
                   {QLatin1String("contents"), QJsonArray{QJsonObject{{QLatin1String("uri"), uri},
                                                                      {QLatin1String("mimeType"), resource->mimeType},
                                                                      {QLatin1String("text"), *content}}}}});
}

void McpSession::getPrompt(const QJsonValue &id, const QJsonObject &params)
{
    const QString name = params.value(QLatin1String("name")).toString();
    const McpPrompt *prompt = registry_->prompt(name);
    if (!prompt) {
        error(id, invalidParams, QStringLiteral("Unknown prompt %1").arg(name));
        return;
    }
    const QJsonObject arguments = params.value(QLatin1String("arguments")).toObject();
    for (const McpPromptArgument &argument : prompt->arguments) {
        if (argument.required && arguments.value(argument.name).toString().isEmpty()) {
            error(id, invalidParams, QStringLiteral("Missing argument \"%1\"").arg(argument.name));
            return;
        }
    }
    result(id, QJsonObject{{QLatin1String("description"), prompt->description},
                           {QLatin1String("messages"),
                            QJsonArray{QJsonObject{{QLatin1String("role"), QLatin1String("user")},
                                                   {QLatin1String("content"),
                                                    QJsonObject{{QLatin1String("type"), QLatin1String("text")},
                                                                {QLatin1String("text"), prompt->text(arguments)}}}}}}});
}
