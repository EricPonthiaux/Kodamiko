#include "claude/ClaudeLauncher.h"
#include "claude/HookServer.h"
#include "claude/mcp/McpRegistry.h"
#include "claude/mcp/McpSession.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

namespace {

// A registry with a few tools of every kind.
void fill(McpRegistry &registry)
{
    using namespace McpSchema;
    registry.instructions = QStringLiteral("Test server");
    registry.addTool({QStringLiteral("echo"),
                      QStringLiteral("Echo"),
                      QStringLiteral("Returns its text"),
                      object({{QStringLiteral("text"), string(QStringLiteral("Text"))},
                              {QStringLiteral("times"), integer(QStringLiteral("Repetitions"))},
                              {QStringLiteral("mode"), enumeration({QStringLiteral("upper"), QStringLiteral("lower")},
                                                                   QStringLiteral("Case"))}},
                             {QStringLiteral("text")}),
                      {},
                      true,
                      false,
                      true,
                      false,
                      {},
                      [](const QJsonObject &arguments, McpReply reply, McpProgress progress) {
                          progress(1, 2, QStringLiteral("halfway"));
                          QString text = arguments.value(QStringLiteral("text"))
                                             .toString()
                                             .repeated(arguments.value(QStringLiteral("times")).toInt(1));
                          if (arguments.value(QStringLiteral("mode")).toString() == QLatin1String("upper"))
                              text = text.toUpper();
                          reply(McpResult::json(QJsonObject{{QStringLiteral("text"), text}}));
                      }});
    registry.addTool({QStringLiteral("erase"),
                      QStringLiteral("Erase everything"),
                      QStringLiteral("Asks first"),
                      object({}),
                      {},
                      false,
                      true,
                      false,
                      true,
                      [](const QJsonObject &) { return QStringLiteral("erase it all"); },
                      [](const QJsonObject &, McpReply reply, McpProgress) {
                          reply(McpResult::message(QStringLiteral("erased")));
                      }});
    registry.addResource({QStringLiteral("duet://test/state"), false, QStringLiteral("state"), QStringLiteral("State"),
                          QStringLiteral("A state"), QStringLiteral("application/json"),
                          [](const QString &) -> std::optional<QString> { return QStringLiteral("{\"a\":1}"); }});
    registry.addResource(
        {QStringLiteral("duet://test/file/{path}"), true, QStringLiteral("file"), QStringLiteral("File"),
         QStringLiteral("A file"), QStringLiteral("text/plain"),
         [](const QString &uri) -> std::optional<QString> { return uri.section(QLatin1Char('/'), -1); }});
    registry.addPrompt({QStringLiteral("review"),
                        QStringLiteral("Review"),
                        QStringLiteral("Reviews a file"),
                        {{QStringLiteral("file"), QStringLiteral("File"), true}},
                        [](const QJsonObject &arguments) {
                            return QStringLiteral("Review %1").arg(arguments.value(QStringLiteral("file")).toString());
                        }});
}

QJsonObject request(int id, const QString &method, const QJsonObject &params = {})
{
    return QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                       {QStringLiteral("id"), id},
                       {QStringLiteral("method"), method},
                       {QStringLiteral("params"), params}};
}

// Sends a message to the relay and returns the next line it writes, while
// this process's event loop (the editor's side) keeps running.
QJsonObject exchange(QProcess &relay, const QJsonObject &message)
{
    if (!message.isEmpty())
        relay.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    QElapsedTimer clock;
    clock.start();
    while (!relay.canReadLine() && clock.elapsed() < 5000)
        QTest::qWait(10);
    return QJsonDocument::fromJson(relay.readLine()).object();
}

} // namespace

class TestMcp : public QObject {
    Q_OBJECT

private slots:

    // The protocol, without a socket.
    void servesTheProtocol()
    {
        McpRegistry registry;
        fill(registry);
        McpSession session(nullptr, &registry);
        QSignalSpy sent(&session, &McpSession::sent);
        const auto last = [&sent] { return sent.last().first().toJsonObject(); };

        session.handleMessage(request(1, QStringLiteral("initialize"),
                                      {{QStringLiteral("protocolVersion"), QStringLiteral("2025-03-26")}}));
        QJsonObject result = last().value(QStringLiteral("result")).toObject();
        QCOMPARE(result.value(QStringLiteral("protocolVersion")).toString(), QStringLiteral("2025-03-26"));
        QCOMPARE(result.value(QStringLiteral("serverInfo")).toObject().value(QStringLiteral("name")).toString(),
                 QStringLiteral("duet"));
        QCOMPARE(result.value(QStringLiteral("instructions")).toString(), QStringLiteral("Test server"));
        QVERIFY(result.value(QStringLiteral("capabilities")).toObject().contains(QStringLiteral("tools")));
        // An unknown version gets the newest.
        session.handleMessage(request(2, QStringLiteral("initialize"),
                                      {{QStringLiteral("protocolVersion"), QStringLiteral("1999-01-01")}}));
        QCOMPARE(last().value(QStringLiteral("result")).toObject().value(QStringLiteral("protocolVersion")).toString(),
                 McpSession::protocolVersions().first());

        // Notifications get no answer.
        const qsizetype count = sent.size();
        session.handleMessage(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                          {QStringLiteral("method"), QStringLiteral("notifications/initialized")}});
        QCOMPARE(sent.size(), count);

        session.handleMessage(request(3, QStringLiteral("tools/list")));
        const QJsonArray tools =
            last().value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray();
        QCOMPARE(tools.size(), 2);
        const QJsonObject echo = tools.first().toObject();
        QCOMPARE(echo.value(QStringLiteral("name")).toString(), QStringLiteral("echo"));
        QVERIFY(echo.value(QStringLiteral("annotations")).toObject().value(QStringLiteral("readOnlyHint")).toBool());
        QVERIFY(tools.last()
                    .toObject()
                    .value(QStringLiteral("annotations"))
                    .toObject()
                    .value(QStringLiteral("destructiveHint"))
                    .toBool());

        // A call, with progress reported to its token.
        session.handleMessage(
            request(4, QStringLiteral("tools/call"),
                    {{QStringLiteral("name"), QStringLiteral("echo")},
                     {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("text"), QStringLiteral("ab")},
                                                               {QStringLiteral("times"), 2},
                                                               {QStringLiteral("mode"), QStringLiteral("upper")}}},
                     {QStringLiteral("_meta"), QJsonObject{{QStringLiteral("progressToken"), QStringLiteral("t1")}}}}));
        const QJsonObject progress = sent.at(sent.size() - 2).first().toJsonObject();
        QCOMPARE(progress.value(QStringLiteral("method")).toString(), QStringLiteral("notifications/progress"));
        QCOMPARE(progress.value(QStringLiteral("params")).toObject().value(QStringLiteral("progressToken")).toString(),
                 QStringLiteral("t1"));
        result = last().value(QStringLiteral("result")).toObject();
        QVERIFY(!result.value(QStringLiteral("isError")).toBool());
        QCOMPARE(result.value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("text")).toString(),
                 QStringLiteral("ABAB"));
        QCOMPARE(result.value(QStringLiteral("content"))
                     .toArray()
                     .first()
                     .toObject()
                     .value(QStringLiteral("text"))
                     .toString(),
                 QStringLiteral("{\"text\":\"ABAB\"}"));

        // Protocol errors: unknown tool, missing or mistyped argument,
        // unknown method.
        const auto errorOf = [&](int id, const QJsonObject &params) {
            session.handleMessage(request(id, QStringLiteral("tools/call"), params));
            return last().value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
        };
        QCOMPARE(errorOf(5, {{QStringLiteral("name"), QStringLiteral("nope")}}), QStringLiteral("Unknown tool nope"));
        QCOMPARE(errorOf(6, {{QStringLiteral("name"), QStringLiteral("echo")}}),
                 QStringLiteral("Missing argument \"text\""));
        QCOMPARE(errorOf(7, {{QStringLiteral("name"), QStringLiteral("echo")},
                             {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("text"), 3}}}}),
                 QStringLiteral("Argument \"text\" must be of type string"));
        QCOMPARE(
            errorOf(8, {{QStringLiteral("name"), QStringLiteral("echo")},
                        {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("text"), QStringLiteral("a")},
                                                                  {QStringLiteral("mode"), QStringLiteral("title")}}}}),
            QStringLiteral("Argument \"mode\" must be one of: upper, lower"));
        session.handleMessage(request(9, QStringLiteral("nope/nope")));
        QCOMPARE(last().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32601);

        // Resources, templates, subscriptions.
        session.handleMessage(request(10, QStringLiteral("resources/list")));
        QCOMPARE(last().value(QStringLiteral("result")).toObject().value(QStringLiteral("resources")).toArray().size(),
                 1);
        session.handleMessage(request(11, QStringLiteral("resources/templates/list")));
        QCOMPARE(last()
                     .value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("resourceTemplates"))
                     .toArray()
                     .first()
                     .toObject()
                     .value(QStringLiteral("uriTemplate"))
                     .toString(),
                 QStringLiteral("duet://test/file/{path}"));
        session.handleMessage(request(12, QStringLiteral("resources/read"),
                                      {{QStringLiteral("uri"), QStringLiteral("duet://test/file/a.txt")}}));
        QCOMPARE(last()
                     .value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("contents"))
                     .toArray()
                     .first()
                     .toObject()
                     .value(QStringLiteral("text"))
                     .toString(),
                 QStringLiteral("a.txt"));
        session.handleMessage(
            request(13, QStringLiteral("resources/read"), {{QStringLiteral("uri"), QStringLiteral("duet://x")}}));
        QCOMPARE(last().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32002);
        session.handleMessage(request(14, QStringLiteral("resources/subscribe"),
                                      {{QStringLiteral("uri"), QStringLiteral("duet://test/state")}}));
        registry.notifyResourceUpdated(QStringLiteral("duet://test/other"));
        QVERIFY(last().contains(QStringLiteral("result"))); // not subscribed: nothing sent
        registry.notifyResourceUpdated(QStringLiteral("duet://test/state"));
        QCOMPARE(last().value(QStringLiteral("method")).toString(), QStringLiteral("notifications/resources/updated"));

        // Prompts.
        session.handleMessage(
            request(15, QStringLiteral("prompts/get"),
                    {{QStringLiteral("name"), QStringLiteral("review")},
                     {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("file"), QStringLiteral("a.cpp")}}}}));
        QCOMPARE(last()
                     .value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("messages"))
                     .toArray()
                     .first()
                     .toObject()
                     .value(QStringLiteral("content"))
                     .toObject()
                     .value(QStringLiteral("text"))
                     .toString(),
                 QStringLiteral("Review a.cpp"));
        session.handleMessage(
            request(16, QStringLiteral("prompts/get"), {{QStringLiteral("name"), QStringLiteral("review")}}));
        QCOMPARE(last().value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString(),
                 QStringLiteral("Missing argument \"file\""));
    }

    // A tool that changes things runs only once the user agreed.
    void asksBeforeSensitiveTools()
    {
        McpRegistry registry;
        fill(registry);
        McpSession session(nullptr, &registry);
        QSignalSpy sent(&session, &McpSession::sent);
        const auto call = [&](int id) {
            session.handleMessage(
                request(id, QStringLiteral("tools/call"), {{QStringLiteral("name"), QStringLiteral("erase")}}));
        };

        // Without anyone to ask: refused.
        call(1);
        QVERIFY(sent.last()
                    .first()
                    .toJsonObject()
                    .value(QStringLiteral("result"))
                    .toObject()
                    .value(QStringLiteral("isError"))
                    .toBool());

        QString asked;
        std::function<void(bool)> answer;
        registry.setConfirmer([&](const QString &, const QString &what, std::function<void(bool)> reply) {
            asked = what;
            answer = std::move(reply);
        });
        call(2);
        QCOMPARE(asked, QStringLiteral("erase it all"));
        const qsizetype before = sent.size();
        answer(true);
        QCOMPARE(sent.size(), before + 1);
        QCOMPARE(sent.last()
                     .first()
                     .toJsonObject()
                     .value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("content"))
                     .toArray()
                     .first()
                     .toObject()
                     .value(QStringLiteral("text"))
                     .toString(),
                 QStringLiteral("erased"));
        call(3);
        answer(false);
        const QJsonObject refused = sent.last().first().toJsonObject().value(QStringLiteral("result")).toObject();
        QVERIFY(refused.value(QStringLiteral("isError")).toBool());

        // Read-only tools are allowed without asking in Claude Code; the
        // others are not.
        QCOMPARE(registry.readOnlyToolPermissions(QStringLiteral("duet")),
                 QStringList{QStringLiteral("mcp__duet__echo")});
    }

    // The relay Claude Code starts, against the editor's socket.
    void relaysThroughDuetMcp()
    {
        McpRegistry registry;
        fill(registry);
        HookServer server;
        QVERIFY(server.listen());
        server.setMcpRegistry(&registry);

        QProcess relay;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("DUET_SOCKET"), server.serverName());
        relay.setProcessEnvironment(environment);
        relay.start(QStringLiteral(DUET_MCP_PATH), {});
        QVERIFY(relay.waitForStarted());
        QJsonObject reply = exchange(relay, request(1, QStringLiteral("initialize")));
        QCOMPARE(reply.value(QStringLiteral("id")).toInt(), 1);
        QCOMPARE(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("instructions")).toString(),
                 QStringLiteral("Test server"));
        reply = exchange(relay, request(2, QStringLiteral("tools/call"),
                                        {{QStringLiteral("name"), QStringLiteral("echo")},
                                         {QStringLiteral("arguments"),
                                          QJsonObject{{QStringLiteral("text"), QStringLiteral("hé")}}}}));
        QCOMPARE(reply.value(QStringLiteral("result"))
                     .toObject()
                     .value(QStringLiteral("structuredContent"))
                     .toObject()
                     .value(QStringLiteral("text"))
                     .toString(),
                 QStringLiteral("hé"));

        // Hooks still work on the same socket meanwhile.
        QSignalSpy hooks(&server, &HookServer::hookReceived);
        QProcess hook;
        hook.setProcessEnvironment(environment);
        hook.start(QStringLiteral(DUET_HOOK_PATH), {});
        hook.write(R"({"hook_event_name":"Stop","session_id":"s"})");
        hook.closeWriteChannel();
        QTRY_COMPARE_WITH_TIMEOUT(hook.state(), QProcess::NotRunning, 5000);
        QCOMPARE(hooks.size(), 1);

        // Claude Code closing our input ends the relay.
        relay.closeWriteChannel();
        QTRY_COMPARE_WITH_TIMEOUT(relay.state(), QProcess::NotRunning, 5000);
        QCOMPARE(relay.exitCode(), 0);
    }

    // Without an editor, the relay still answers: a server with no tools.
    void answersWithoutTheEditor()
    {
        QProcess relay;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("DUET_SOCKET"), QStringLiteral("duet-nobody-listens"));
        relay.setProcessEnvironment(environment);
        relay.start(QStringLiteral(DUET_MCP_PATH), {});
        QVERIFY(relay.waitForStarted());
        QJsonObject reply = exchange(relay, request(1, QStringLiteral("initialize")));
        QVERIFY(reply.value(QStringLiteral("result"))
                    .toObject()
                    .value(QStringLiteral("instructions"))
                    .toString()
                    .startsWith(QStringLiteral("Duet's editor is not reachable")));
        reply = exchange(relay, request(2, QStringLiteral("tools/list")));
        QVERIFY(reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray().isEmpty());
        reply = exchange(relay,
                         request(3, QStringLiteral("tools/call"), {{QStringLiteral("name"), QStringLiteral("echo")}}));
        QVERIFY(reply.contains(QStringLiteral("error")));
        relay.closeWriteChannel();
        QTRY_COMPARE_WITH_TIMEOUT(relay.state(), QProcess::NotRunning, 5000);
    }

    // The editor going away fails what it had not answered.
    void failsPendingCallsWhenTheEditorCloses()
    {
        McpRegistry registry;
        McpReply held;
        registry.addTool({QStringLiteral("slow"),
                          {},
                          QStringLiteral("Never answers"),
                          {},
                          {},
                          true,
                          false,
                          false,
                          false,
                          {},
                          [&held](const QJsonObject &, McpReply reply, McpProgress) { held = reply; }});
        auto *server = new HookServer;
        QVERIFY(server->listen());
        server->setMcpRegistry(&registry);
        QProcess relay;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("DUET_SOCKET"), server->serverName());
        relay.setProcessEnvironment(environment);
        relay.start(QStringLiteral(DUET_MCP_PATH), {});
        QVERIFY(relay.waitForStarted());
        relay.write(
            QJsonDocument(request(7, QStringLiteral("tools/call"), {{QStringLiteral("name"), QStringLiteral("slow")}}))
                .toJson(QJsonDocument::Compact) +
            '\n');
        QTRY_VERIFY_WITH_TIMEOUT(held != nullptr, 5000);
        delete server; // the editor closes
        const QJsonObject reply = exchange(relay, {});
        QCOMPARE(reply.value(QStringLiteral("id")).toInt(), 7);
        QVERIFY(reply.contains(QStringLiteral("error")));
        relay.closeWriteChannel();
        QTRY_COMPARE_WITH_TIMEOUT(relay.state(), QProcess::NotRunning, 5000);
    }

    // The session's launch declares the server and its read-only tools.
    void declaresTheServerToClaude()
    {
        const QJsonObject config =
            QJsonDocument::fromJson(ClaudeLauncher::mcpConfig(QStringLiteral("/opt/duet/duet-mcp"))).object();
        const QJsonObject duet =
            config.value(QStringLiteral("mcpServers")).toObject().value(QStringLiteral("duet")).toObject();
        QCOMPARE(duet.value(QStringLiteral("type")).toString(), QStringLiteral("stdio"));
        QCOMPARE(duet.value(QStringLiteral("command")).toString(),
                 QDir::toNativeSeparators(QStringLiteral("/opt/duet/duet-mcp")));
        const QJsonObject settings =
            QJsonDocument::fromJson(
                ClaudeLauncher::sessionSettings(QStringLiteral("hook"), {QStringLiteral("mcp__duet__a")}))
                .object();
        QCOMPARE(settings.value(QStringLiteral("permissions")).toObject().value(QStringLiteral("allow")).toArray(),
                 QJsonArray{QStringLiteral("mcp__duet__a")});
        QVERIFY(!QJsonDocument::fromJson(ClaudeLauncher::sessionSettings(QStringLiteral("hook")))
                     .object()
                     .contains(QStringLiteral("permissions")));
    }
};

QTEST_MAIN(TestMcp)
#include "tst_mcp.moc"
