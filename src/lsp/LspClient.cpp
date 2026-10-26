// SPDX-License-Identifier: GPL-3.0-only

#include "LspClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QUrl>

#include <ranges>
#include <utility>

namespace {

// What Duet understands of the protocol: completion with snippets (the
// first argument selected) and edits resolved once chosen (imports).
QJsonObject clientCapabilities()
{
    const QJsonObject completionItem{
        {"snippetSupport", true},
        {"resolveSupport", QJsonObject{{"properties", QJsonArray{"additionalTextEdits", "detail", "documentation"}}}},
        {"documentationFormat", QJsonArray{"plaintext"}},
        {"insertReplaceSupport", false},
        {"labelDetailsSupport", true},
    };
    const QJsonObject completion{
        {"completionItem", completionItem},
        {"contextSupport", true},
    };
    const QJsonObject synchronization{{"didSave", true}, {"willSave", false}, {"dynamicRegistration", false}};
    // Quick fixes and refactorings, as edits or as commands the server
    // carries out (it then sends "workspace/applyEdit").
    const QJsonObject codeAction{
        {"codeActionLiteralSupport",
         QJsonObject{{"codeActionKind",
                      QJsonObject{{"valueSet", QJsonArray{"", "quickfix", "refactor", "refactor.extract",
                                                          "refactor.inline", "refactor.rewrite", "source"}}}}}},
    };
    const QJsonObject signatureHelp{
        {"signatureInformation", QJsonObject{{"documentationFormat", QJsonArray{"plaintext"}},
                                             {"parameterInformation", QJsonObject{{"labelOffsetSupport", true}}}}},
    };
    const QJsonObject textDocument{
        {"completion", completion},
        {"synchronization", synchronization},
        {"hover", QJsonObject{{"contentFormat", QJsonArray{"markdown", "plaintext"}}}},
        {"signatureHelp", signatureHelp},
        {"definition", QJsonObject{{"linkSupport", true}}},
        {"declaration", QJsonObject{{"linkSupport", true}}},
        {"implementation", QJsonObject{{"linkSupport", true}}},
        {"typeDefinition", QJsonObject{{"linkSupport", true}}},
        {"callHierarchy", QJsonObject{}},
        {"typeHierarchy", QJsonObject{}},
        {"references", QJsonObject{}},
        {"documentHighlight", QJsonObject{}},
        {"rename", QJsonObject{{"prepareSupport", true}}},
        {"codeAction", codeAction},
    };
    const QJsonObject workspace{
        {"applyEdit", true},
        {"workspaceEdit",
         QJsonObject{{"documentChanges", true}, {"resourceOperations", QJsonArray{"create", "rename", "delete"}}}},
        {"configuration", true},
    };
    return QJsonObject{
        {"textDocument", textDocument},
        {"workspace", workspace},
        // Long tasks (indexing) told with "$/progress".
        {"window", QJsonObject{{"workDoneProgress", true}}},
        {"general", QJsonObject{{"positionEncodings", QJsonArray{"utf-16"}}}},
    };
}

// The Content-Length of a message's header lines, if they have one.
std::optional<qsizetype> contentLength(QByteArrayView headers)
{
    static constexpr QByteArrayView name = "content-length:";
    std::optional<qsizetype> length;
    for (qsizetype lineStart = 0; lineStart < headers.size();) {
        qsizetype lineEnd = headers.indexOf('\n', lineStart);
        if (lineEnd < 0)
            lineEnd = headers.size();
        const QByteArrayView header = headers.sliced(lineStart, lineEnd - lineStart).trimmed();
        if (header.size() >= name.size() && header.first(name.size()).compare(name, Qt::CaseInsensitive) == 0)
            length = header.sliced(name.size()).trimmed().toLongLong();
        lineStart = lineEnd + 1;
    }
    return length;
}

// `uri`, if it is the folder `from` or inside it, under the folder `to`.
QString respelled(const QString &uri, const QString &from, const QString &to)
{
    const bool isInside = uri.startsWith(from) && (uri.size() == from.size() || uri.at(from.size()) == u'/');
    return isInside ? to + uri.sliced(from.size()) : uri;
}

// `value` with every URI (string or object key) under `from` respelled
// under `to`.
QJsonValue respelledUris(const QJsonValue &value, const QString &from, const QString &to)
{
    if (value.isString())
        return respelled(value.toString(), from, to);
    if (value.isArray()) {
        QJsonArray array;
        for (const QJsonValue &item : value.toArray())
            array.append(respelledUris(item, from, to));
        return array;
    }
    if (value.isObject()) {
        QJsonObject object;
        const QJsonObject original = value.toObject();
        for (auto it = original.begin(); it != original.end(); ++it)
            object.insert(respelled(it.key(), from, to), respelledUris(it.value(), from, to));
        return object;
    }
    return value;
}

QString folderUri(const QString &folder)
{
    return QUrl::fromLocalFile(folder).toString(QUrl::FullyEncoded);
}

} // namespace

LspClient::LspClient(QString program, QStringList arguments, const QString &rootFolder, QObject *parent)
    : QObject(parent)
    , program_(std::move(program))
    , arguments_(std::move(arguments))
    , rootFolder_(rootFolder)
    , process_(new QProcess(this))
{
    // Servers resolve the symbolic links of some paths (clangd's index; /var
    // is /private/var on macOS) and not others: the server is only told the
    // resolved paths, and its answers are named back as the project is, so
    // that one file has one path on each side.
    if (const QString canonical = QFileInfo(rootFolder).canonicalFilePath();
        !canonical.isEmpty() && canonical != QDir::cleanPath(rootFolder)) {
        uriRespelling_ = {.from = folderUri(canonical), .to = folderUri(QDir::cleanPath(rootFolder))};
    }
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    process_->setWorkingDirectory(rootFolder);
    connect(process_, &QProcess::readyReadStandardOutput, this, &LspClient::onOutput);
    // Servers log a lot there: only the end is kept, to say why one stopped.
    connect(process_, &QProcess::readyReadStandardError, this, [this] {
        errors_.append(process_->readAllStandardError());
        if (errors_.size() > 8192)
            errors_ = errors_.right(4096);
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || failed_)
            return;
        failed_ = true;
        emit failed(tr("Cannot start %1: %2").arg(QFileInfo(program_).fileName(), process_->errorString()));
    });
    connect(process_, &QProcess::finished, this, [this] {
        ready_ = false;
        if (stopping_ || failed_)
            return;
        failed_ = true;
        // The requests in flight will get no answer.
        const QHash<int, ResponseHandler> pending = std::exchange(pending_, {});
        for (const ResponseHandler &handler : pending)
            handler({}, tr("The language server stopped"));
        const QString reason = lastError();
        emit failed(reason.isEmpty() ? tr("%1 stopped").arg(QFileInfo(program_).fileName())
                                     : tr("%1 stopped: %2").arg(QFileInfo(program_).fileName(), reason));
    });
}

void LspClient::setEnvironment(const QProcessEnvironment &environment)
{
    process_->setProcessEnvironment(environment);
}

QString LspClient::lastError() const
{
    // The last line that says something ("Exception: jdtls requires at
    // least Java 21"), not a stack frame.
    const QStringList lines = QString::fromUtf8(errors_).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const auto &it : std::views::reverse(lines)) {
        const QString line = it.trimmed();
        if (!line.isEmpty() && !line.startsWith(QLatin1String("at ")) && !line.startsWith(QLatin1String("File \"")) &&
            !line.startsWith(QLatin1Char('^')) && !line.startsWith(QLatin1Char('~')))
            return line.left(240);
    }
    return {};
}

LspClient::~LspClient()
{
    stopping_ = true;
    disconnect(process_, nullptr, this, nullptr);
    if (process_->state() == QProcess::NotRunning)
        return; // deleted with this object
    // The server is left to stop on its own, without blocking the UI (a
    // project switch, quitting): its QProcess outlives this object and is
    // deleted once the server has exited.
    process_->setParent(nullptr);
    connect(process_, &QProcess::finished, process_, &QObject::deleteLater);
    if (ready_) {
        // "shutdown" then "exit", as the protocol asks; no time to wait for
        // the answer.
        send(QJsonObject{{"jsonrpc", "2.0"}, {"id", nextId_++}, {"method", "shutdown"}});
        send(QJsonObject{{"jsonrpc", "2.0"}, {"method", "exit"}});
        process_->closeWriteChannel();
        QTimer::singleShot(1500, process_, &QProcess::kill);
    } else {
        process_->kill();
    }
}

void LspClient::start()
{
    if (process_->state() != QProcess::NotRunning || failed_)
        return;
    process_->start(program_, arguments_);

    QJsonObject params{
        {"processId", QCoreApplication::applicationPid()},
        {"clientInfo", QJsonObject{{"name", "Duet"}}},
        {"rootUri", QUrl::fromLocalFile(rootFolder_).toString()},
        {"workspaceFolders", QJsonArray{QJsonObject{{"uri", QUrl::fromLocalFile(rootFolder_).toString()},
                                                    {"name", QFileInfo(rootFolder_).fileName()}}}},
        {"capabilities", clientCapabilities()},
    };
    if (!initializationOptions_.isEmpty())
        params.insert("initializationOptions", initializationOptions_);
    const int id = nextId_++;
    pending_.insert(id, [this](const QJsonValue &result, const QString &error) {
        if (!error.isEmpty()) {
            failed_ = true;
            emit failed(error);
            return;
        }
        capabilities_ = result.toObject().value("capabilities").toObject();
        ready_ = true;
        send(QJsonObject{{"jsonrpc", "2.0"}, {"method", "initialized"}, {"params", QJsonObject{}}});
        if (!settings_.isEmpty())
            send(QJsonObject{{"jsonrpc", "2.0"},
                             {"method", "workspace/didChangeConfiguration"},
                             {"params", QJsonObject{{"settings", settings_}}}});
        for (const QJsonObject &message : std::exchange(waiting_, {}))
            send(message);
        emit ready();
    });
    send(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"method", "initialize"}, {"params", params}});
}

bool LspClient::isRunning() const
{
    return process_->state() != QProcess::NotRunning;
}

LspClient::RequestId LspClient::request(const QString &method,
                                        const QJsonObject &params,
                                        ResponseHandler handler,
                                        std::optional<std::chrono::milliseconds> timeout)
{
    const int id = nextId_++;
    if (failed_) {
        handler({}, tr("The language server is not running"));
        return {id};
    }
    pending_.insert(id, std::move(handler));
    const QJsonObject message{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
    if (ready_)
        send(message);
    else
        waiting_.append(message);
    if (timeout)
        QTimer::singleShot(*timeout, this, [this, id, timeout] { expire(id, *timeout); });
    return {id};
}

void LspClient::cancel(RequestId id)
{
    if (!pending_.remove(id.value))
        return;
    const auto isTheRequest = [&](const QJsonObject &message) { return message.value("id").toInt() == id.value; };
    // Not sent yet: it never will be.
    if (waiting_.removeIf(isTheRequest) > 0)
        return;
    send(QJsonObject{{"jsonrpc", "2.0"}, {"method", "$/cancelRequest"}, {"params", QJsonObject{{"id", id.value}}}});
}

void LspClient::expire(int id, std::chrono::milliseconds timeout)
{
    const ResponseHandler handler = pending_.value(id);
    if (!handler)
        return;
    cancel({id});
    const auto seconds = std::chrono::duration_cast<std::chrono::duration<double>>(timeout).count();
    handler({}, tr("No answer from %1 after %2 s").arg(QFileInfo(program_).fileName()).arg(seconds));
}

void LspClient::notify(const QString &method, const QJsonObject &params)
{
    if (failed_)
        return;
    const QJsonObject message{{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
    if (ready_)
        send(message);
    else
        waiting_.append(message);
}

QByteArray LspClient::frame(const QJsonObject &message)
{
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    return "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

void LspClient::send(const QJsonObject &message)
{
    if (process_->state() == QProcess::NotRunning)
        return;
    if (uriRespelling_)
        process_->write(frame(respelledUris(message, uriRespelling_->to, uriRespelling_->from).toObject()));
    else
        process_->write(frame(message));
}

QList<QJsonObject> LspClient::takeMessages(QByteArray &buffer)
{
    // Messages are read at an offset, and the buffer is shortened once:
    // removing each message from the front would be quadratic when many
    // arrive together (diagnostics, progress).
    QList<QJsonObject> messages;
    qsizetype offset = 0;
    for (;;) {
        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n", offset);
        if (headerEnd < 0)
            break;
        const std::optional<qsizetype> length =
            contentLength(QByteArrayView(buffer.constData() + offset, headerEnd - offset));
        const bool isHeader = length && *length >= 0;
        if (!isHeader) { // not a header: drop it
            offset = headerEnd + 4;
            continue;
        }
        if (buffer.size() < headerEnd + 4 + *length)
            break; // the rest is still coming
        const QJsonDocument document =
            QJsonDocument::fromJson(QByteArray::fromRawData(buffer.constData() + headerEnd + 4, *length));
        offset = headerEnd + 4 + *length;
        if (document.isObject())
            messages.append(document.object());
    }
    buffer.remove(0, offset);
    return messages;
}

QJsonValue LspClient::settingAt(const QJsonObject &settings, const QString &section)
{
    QJsonValue value = settings;
    for (const QString &key : section.split(QLatin1Char('.'), Qt::SkipEmptyParts)) {
        if (!value.isObject())
            return QJsonValue::Null;
        value = value.toObject().value(key);
    }
    return value.isUndefined() ? QJsonValue(QJsonValue::Null) : value;
}

void LspClient::onOutput()
{
    buffer_.append(process_->readAllStandardOutput());
    for (const QJsonObject &message : takeMessages(buffer_)) {
        if (uriRespelling_)
            handle(respelledUris(message, uriRespelling_->from, uriRespelling_->to).toObject());
        else
            handle(message);
    }
}

void LspClient::handle(const QJsonObject &message)
{
    const QJsonValue id = message.value("id");
    if (message.contains("method")) {
        // A request from the server (configuration, progress…): answered
        // with nothing, which servers accept; for a configuration request,
        // the setting of each item's section.
        if (id.isUndefined() || id.isNull()) {
            emit notificationReceived(message.value("method").toString(), message.value("params"));
            return;
        }
        QJsonValue result = QJsonValue::Null;
        if (const auto handler = requestHandlers_.constFind(message.value("method").toString());
            handler != requestHandlers_.cend()) {
            result = (*handler)(message.value("params"));
        } else if (message.value("method").toString() == QLatin1String("workspace/configuration")) {
            QJsonArray settings;
            for (const QJsonValue &item : message.value("params").toObject().value("items").toArray())
                settings.append(settingAt(settings_, item.toObject().value("section").toString()));
            result = settings;
        }
        send(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
        return;
    }
    const auto it = pending_.find(id.toInt());
    if (it == pending_.end())
        return;
    const ResponseHandler handler = it.value();
    pending_.erase(it);
    const QJsonObject error = message.value("error").toObject();
    handler(message.value("result"), error.isEmpty() ? QString() : error.value("message").toString());
}
