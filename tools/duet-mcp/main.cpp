// duet-mcp: the editor's MCP server, as Claude Code starts it.
//
// Claude Code runs this program for the "duet" MCP server declared with
// --mcp-config (see ClaudeLauncher) and speaks MCP with it over standard
// input and output: JSON-RPC, one message per line. We relay those lines as
// they are to the editor, over the local socket named in DUET_SOCKET, after
// a first line {"duet":"mcp"} that tells it apart from hooks; the editor
// serves the tools and answers on the same socket.
//
// When the editor cannot be reached (closed, or another one), we answer on
// its behalf: the server initializes with no tools and says why, so that
// Claude never waits for a reply that will not come.

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QSet>

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace {

constexpr int connectTimeoutMs = 2000;

void writeLine(const QByteArray &line)
{
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void answer(const QJsonValue &id, const QJsonObject &result)
{
    writeLine(QJsonDocument(QJsonObject{{QLatin1String("jsonrpc"), QLatin1String("2.0")},
                                        {QLatin1String("id"), id},
                                        {QLatin1String("result"), result}})
                  .toJson(QJsonDocument::Compact));
}

void fail(const QJsonValue &id, const QString &message)
{
    writeLine(QJsonDocument(QJsonObject{{QLatin1String("jsonrpc"), QLatin1String("2.0")},
                                        {QLatin1String("id"), id},
                                        {QLatin1String("error"), QJsonObject{{QLatin1String("code"), -32603},
                                                                             {QLatin1String("message"), message}}}})
                  .toJson(QJsonDocument::Compact));
}

const QString unreachable = QStringLiteral("Duet's editor is not reachable: its tools are not available in this "
                                           "session (restart Claude from Duet).");

// Answers in the editor's absence: an initialized server with nothing to
// offer.
void answerAlone(const QJsonObject &message)
{
    if (!message.contains(QLatin1String("id")))
        return;
    const QJsonValue id = message.value(QLatin1String("id"));
    const QString method = message.value(QLatin1String("method")).toString();
    if (method == QLatin1String("initialize")) {
        const QString version = message.value(QLatin1String("params"))
                                    .toObject()
                                    .value(QLatin1String("protocolVersion"))
                                    .toString(QStringLiteral("2025-06-18"));
        answer(id,
               QJsonObject{{QLatin1String("protocolVersion"), version},
                           {QLatin1String("capabilities"), QJsonObject{{QLatin1String("tools"), QJsonObject{}}}},
                           {QLatin1String("serverInfo"), QJsonObject{{QLatin1String("name"), QLatin1String("duet")},
                                                                     {QLatin1String("version"), QLatin1String("0")}}},
                           {QLatin1String("instructions"), unreachable}});
    } else if (method == QLatin1String("ping")) {
        answer(id, {});
    } else if (method == QLatin1String("tools/list")) {
        answer(id, QJsonObject{{QLatin1String("tools"), QJsonArray{}}});
    } else if (method == QLatin1String("resources/list")) {
        answer(id, QJsonObject{{QLatin1String("resources"), QJsonArray{}}});
    } else if (method == QLatin1String("prompts/list")) {
        answer(id, QJsonObject{{QLatin1String("prompts"), QJsonArray{}}});
    } else {
        fail(id, unreachable);
    }
}

class Relay : public QObject {
public:
    Relay()
    {
        const QString name = qEnvironmentVariable("DUET_SOCKET");
        if (!name.isEmpty()) {
            socket_.connectToServer(name);
            if (socket_.waitForConnected(connectTimeoutMs)) {
                socket_.write("{\"duet\":\"mcp\"}\n");
                socket_.flush();
                connected_ = true;
            }
        }
        connect(&socket_, &QLocalSocket::readyRead, this, [this] {
            while (socket_.canReadLine()) {
                const QByteArray line = socket_.readLine().trimmed();
                const QJsonObject message = QJsonDocument::fromJson(line).object();
                if (message.contains(QLatin1String("id")) && !message.contains(QLatin1String("method")))
                    pending_.remove(message.value(QLatin1String("id")).toVariant().toString());
                writeLine(line);
            }
        });
        connect(&socket_, &QLocalSocket::disconnected, this, [this] {
            // The editor went away: what it had not answered fails, and we
            // answer from now on.
            connected_ = false;
            for (const QString &id : std::as_const(pending_))
                fail(QJsonValue::fromVariant(pendingIds_.value(id)), unreachable);
            pending_.clear();
        });
    }

    ~Relay() override
    {
        // The socket, closing, would still report its disconnection to us.
        socket_.disconnect(this);
    }

    void receive(const QByteArray &line)
    {
        const QJsonObject message = QJsonDocument::fromJson(line).object();
        if (!connected_) {
            answerAlone(message);
            return;
        }
        if (message.contains(QLatin1String("id")) && message.contains(QLatin1String("method"))) {
            const QVariant id = message.value(QLatin1String("id")).toVariant();
            pending_.insert(id.toString());
            pendingIds_.insert(id.toString(), id);
        }
        socket_.write(line + '\n');
        socket_.flush();
    }

private:
    QLocalSocket socket_;
    bool connected_ = false;
    QSet<QString> pending_;
    QHash<QString, QVariant> pendingIds_;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    Relay relay;
    // Standard input is read on a thread of its own (no portable way to wait
    // on it in the event loop); each line is handed to the relay.
    std::thread reader([&relay] {
        std::string line;
        char buffer[65536];
        while (std::fgets(buffer, sizeof buffer, stdin)) {
            line += buffer;
            if (line.empty() || line.back() != '\n')
                continue;
            QByteArray message = QByteArray::fromStdString(line).trimmed();
            line.clear();
            if (message.isEmpty())
                continue;
            QMetaObject::invokeMethod(&relay, [&relay, message] { relay.receive(message); }, Qt::QueuedConnection);
        }
        // Claude Code closed our input: the session is over.
        QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    });
    reader.detach();
    return app.exec();
}
