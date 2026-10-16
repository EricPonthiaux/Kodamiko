#include "LspClient.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QUrl>

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
    const QJsonObject synchronization{{"didSave", false}, {"willSave", false}, {"dynamicRegistration", false}};
    // Quick fixes and refactorings, as edits or as commands the server
    // carries out (it then sends "workspace/applyEdit").
    const QJsonObject codeAction{
        {"codeActionLiteralSupport",
         QJsonObject{{"codeActionKind",
                      QJsonObject{{"valueSet", QJsonArray{"", "quickfix", "refactor", "refactor.extract",
                                                          "refactor.inline", "refactor.rewrite", "source"}}}}}},
    };
    const QJsonObject signatureHelp{
        {"signatureInformation",
         QJsonObject{{"documentationFormat", QJsonArray{"plaintext"}},
                     {"parameterInformation", QJsonObject{{"labelOffsetSupport", true}}}}},
    };
    const QJsonObject textDocument{
        {"completion", completion},
        {"synchronization", synchronization},
        {"hover", QJsonObject{{"contentFormat", QJsonArray{"plaintext"}}}},
        {"signatureHelp", signatureHelp},
        {"definition", QJsonObject{{"linkSupport", true}}},
        {"declaration", QJsonObject{{"linkSupport", true}}},
        {"references", QJsonObject{}},
        {"rename", QJsonObject{{"prepareSupport", false}}},
        {"codeAction", codeAction},
    };
    const QJsonObject workspace{
        {"applyEdit", true},
        {"workspaceEdit", QJsonObject{{"documentChanges", true}}},
        {"configuration", true},
    };
    return QJsonObject{
        {"textDocument", textDocument},
        {"workspace", workspace},
        {"general", QJsonObject{{"positionEncodings", QJsonArray{"utf-16"}}}},
    };
}

} // namespace

LspClient::LspClient(const QString &program, const QStringList &arguments, const QString &rootFolder,
                     QObject *parent)
    : QObject(parent)
    , program_(program)
    , arguments_(arguments)
    , rootFolder_(rootFolder)
    , process_(new QProcess(this))
{
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
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QString line = it->trimmed();
        if (!line.isEmpty() && !line.startsWith(QLatin1String("at ")) && !line.startsWith(QLatin1String("File \""))
            && !line.startsWith(QLatin1Char('^')) && !line.startsWith(QLatin1Char('~')))
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
        {"workspaceFolders",
         QJsonArray{QJsonObject{{"uri", QUrl::fromLocalFile(rootFolder_).toString()},
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

void LspClient::request(const QString &method, const QJsonObject &params, ResponseHandler handler)
{
    if (failed_) {
        handler({}, tr("The language server is not running"));
        return;
    }
    const int id = nextId_++;
    pending_.insert(id, std::move(handler));
    const QJsonObject message{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
    if (ready_)
        send(message);
    else
        waiting_.append(message);
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
    if (process_->state() != QProcess::NotRunning)
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
        qsizetype length = -1;
        for (qsizetype lineStart = offset; lineStart < headerEnd;) {
            qsizetype lineEnd = buffer.indexOf('\n', lineStart);
            if (lineEnd < 0 || lineEnd > headerEnd)
                lineEnd = headerEnd;
            const QByteArrayView header =
                QByteArrayView(buffer.constData() + lineStart, lineEnd - lineStart).trimmed();
            if (header.size() >= 15 && header.first(15).compare("content-length:", Qt::CaseInsensitive) == 0)
                length = header.sliced(15).trimmed().toLongLong();
            lineStart = lineEnd + 1;
        }
        if (length < 0) { // not a header: drop it
            offset = headerEnd + 4;
            continue;
        }
        if (buffer.size() < headerEnd + 4 + length)
            break; // the rest is still coming
        const QJsonDocument document =
            QJsonDocument::fromJson(QByteArray::fromRawData(buffer.constData() + headerEnd + 4, length));
        offset = headerEnd + 4 + length;
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
    for (const QJsonObject &message : takeMessages(buffer_))
        handle(message);
}

void LspClient::handle(const QJsonObject &message)
{
    const QJsonValue id = message.value("id");
    if (message.contains("method")) {
        // A request from the server (configuration, progress…): answered
        // with nothing, which servers accept; for a configuration request,
        // the setting of each item's section.
        if (id.isUndefined() || id.isNull())
            return;
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
