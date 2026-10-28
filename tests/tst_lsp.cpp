// SPDX-License-Identifier: GPL-3.0-only

#include "lsp/LanguageServers.h"
#include "lsp/LspClient.h"
#include "lsp/LspCompletionProvider.h"
#include "lsp/LspProtocol.h"
#include "lsp/ServerTasksLabel.h"
#include "system/Executables.h"
#include "toolchains/JavaRuntimes.h"
#include "toolchains/Toolchains.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// A project whose C++ server is the fake server, installed in `toolchains`
// as a downloaded clangd.
ProjectConfig projectRunningTheFakeServerAsClangd(const QString &path, const Toolchains &toolchains)
{
    const QString folder = toolchains.installFolder("clangd", "23.1.0");
    QDir().mkpath(folder + "/bin");
    const QString clangd = folder + '/' + Executables::fileName("bin/clangd");
    QFile::copy(FAKE_LSP_SERVER_PATH, clangd);
    QFile marker(folder + "/.complete");
    if (marker.open(QIODevice::WriteOnly))
        marker.close();
    ProjectConfig project;
    project.path = path;
    project.languages.append({"cpp", true, "c++20", {}, "clangd", "23.1.0"});
    return project;
}

} // namespace

class TestLsp : public QObject {
    Q_OBJECT

private slots:

    void framesAndSplitsMessages()
    {
        const QJsonObject first{{"jsonrpc", "2.0"}, {"id", 1}, {"result", "é"}};
        const QJsonObject second{{"jsonrpc", "2.0"}, {"method", "window/logMessage"}};
        QByteArray buffer = LspClient::frame(first) + LspClient::frame(second);
        const QByteArray third = LspClient::frame(QJsonObject{{"id", 3}});
        buffer += third.left(third.size() - 4); // still coming

        const QList<QJsonObject> messages = LspClient::takeMessages(buffer);
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages[0], first);
        QCOMPARE(messages[1], second);
        QCOMPARE(buffer, third.left(third.size() - 4)); // kept for later
        buffer += third.right(4);
        QCOMPARE(LspClient::takeMessages(buffer).first().value("id").toInt(), 3);
        QVERIFY(buffer.isEmpty());
    }

    void answersConfigurationRequests()
    {
        const QJsonObject settings{{"python", QJsonObject{{"pythonPath", "/usr/bin/python3"}}}};
        QCOMPARE(LspClient::settingAt(settings, "python.pythonPath").toString(), QString("/usr/bin/python3"));
        QCOMPARE(LspClient::settingAt(settings, "python").toObject(), settings.value("python").toObject());
        QCOMPARE(LspClient::settingAt(settings, "").toObject(), settings);
        QVERIFY(LspClient::settingAt(settings, "java.home").isNull());
        QVERIFY(LspClient::settingAt(settings, "python.pythonPath.x").isNull());
    }

    void tellsServersTheProjectSetup()
    {
        const LanguageSetup cpp{"cpp", true, "c++23", {}, "clangd", "23.1.0"};
        QCOMPARE(LanguageServers::optionsFor("clangd", cpp, {}, {}).initialization.value("fallbackFlags").toArray(),
                 (QJsonArray{"-std=c++23"}));

        LanguageSetup java{"java", true, "17", {}, "jdtls", "1.62.0"};
        const LanguageServers::Options jdtls = LanguageServers::optionsFor("jdtls", java, "/jdk17", "/jdk17/bin/java");
        const QJsonObject runtime =
            LspClient::settingAt(jdtls.settings, "java.configuration.runtimes").toArray().first().toObject();
        QCOMPARE(runtime.value("name").toString(), QString("JavaSE-17"));
        QCOMPARE(runtime.value("path").toString(), QString("/jdk17"));
        QCOMPARE(jdtls.initialization.value("settings").toObject(), jdtls.settings);
        java.runtimeVersion = "8";
        QCOMPARE(LspClient::settingAt(LanguageServers::optionsFor("jdtls", java, "/jdk8", {}).settings,
                                      "java.configuration.runtimes")
                     .toArray()
                     .first()
                     .toObject()
                     .value("name")
                     .toString(),
                 QString("JavaSE-1.8"));

        const LanguageSetup python{"python", true, "3.12", "/p/.venv/bin/python", "pyright", "1.1.414"};
        QCOMPARE(LspClient::settingAt(
                     LanguageServers::optionsFor("pyright", python, "/p/.venv/bin", "/p/.venv/bin/python").settings,
                     "python.pythonPath")
                     .toString(),
                 QString("/p/.venv/bin/python"));
    }

    void configuresCompletionAsIntelliJ()
    {
        // Without a project setup too.
        const LanguageServers::Options jdtls = LanguageServers::optionsFor("jdtls", LanguageSetup(), {}, {});
        QVERIFY(!LspClient::settingAt(jdtls.settings, "java.completion.postfix.enabled").toBool());
        QVERIFY(LspClient::settingAt(jdtls.settings, "java.completion.chain.enabled").toBool());
        QVERIFY(LspClient::settingAt(jdtls.settings, "java.completion.favoriteStaticMembers")
                    .toArray()
                    .contains(QJsonValue("org.junit.jupiter.api.Assertions.*")));
        QVERIFY(LspClient::settingAt(jdtls.settings, "java.completion.filteredTypes")
                    .toArray()
                    .contains(QJsonValue("java.awt.*")));
        QVERIFY(LspClient::settingAt(jdtls.settings, "java.configuration.runtimes").isNull());
        QCOMPARE(jdtls.initialization.value("settings").toObject(), jdtls.settings);
        // With one: the runtime as well.
        const LanguageSetup java{"java", true, "21", {}, "jdtls", "1.62.0"};
        const LanguageServers::Options project = LanguageServers::optionsFor("jdtls", java, "/jdk21", {});
        QVERIFY(!LspClient::settingAt(project.settings, "java.configuration.runtimes").isNull());
        QVERIFY(LspClient::settingAt(project.settings, "java.completion.chain.enabled").toBool());

        const LanguageServers::Options ts =
            LanguageServers::optionsFor("typescript-language-server", LanguageSetup(), {}, {});
        const QJsonObject preferences = ts.initialization.value("preferences").toObject();
        QVERIFY(preferences.value("includeCompletionsForModuleExports").toBool());
        QVERIFY(preferences.value("includeCompletionsWithClassMemberSnippets").toBool());
        QVERIFY(LspClient::settingAt(ts.settings, "completions.completeFunctionCalls").toBool());
    }

    void aRecipeServerServesTheLanguagesNoKnownServerServes()
    {
        const QTemporaryDir dir;
        const QString script = dir.filePath("tool/index.js");
        QDir().mkpath(dir.filePath("tool"));
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const LanguageServerSpec angular{.name = "angular",
                                         .program = script,
                                         .arguments = {"--stdio"},
                                         .languages = {"html"},
                                         .triggerCharacters = "<",
                                         .isNodeScript = true};
        ProjectConfig project;
        project.path = dir.path();
        LanguageServers servers;

        servers.setProject(project, nullptr, {.servers = {angular}, .options = {}});

        QCOMPARE(servers.specFor("html"), std::optional(angular));
        QVERIFY(servers.installed().contains("angular"));
    }

    void aRecipeServerWhoseScriptIsMissingServesNothing()
    {
        const QTemporaryDir dir;
        const LanguageServerSpec angular{.name = "angular",
                                         .program = dir.filePath("not-installed/index.js"),
                                         .arguments = {},
                                         .languages = {"html"},
                                         .triggerCharacters = {},
                                         .isNodeScript = true};
        ProjectConfig project;
        project.path = dir.path();
        LanguageServers servers;

        servers.setProject(project, nullptr, {.servers = {angular}, .options = {}});

        QCOMPARE(servers.specFor("html"), std::nullopt);
    }

    void recipeOptionsAreMergedIntoTheServersOwn()
    {
        const LanguageServers::Options own =
            LanguageServers::optionsFor("typescript-language-server", LanguageSetup(), {}, {});
        const QJsonArray plugins{QJsonObject{{"name", "@angular/language-service"}, {"location", "/p"}}};
        const LanguageServers::Options added{
            .initialization = {{"plugins", plugins}, {"preferences", QJsonObject{{"quotePreference", "single"}}}},
            .settings = {}};

        const LanguageServers::Options merged = LanguageServers::merged(own, added);

        QCOMPARE(merged.initialization.value("plugins").toArray(), plugins);
        const QJsonObject preferences = merged.initialization.value("preferences").toObject();
        QCOMPARE(preferences.value("quotePreference").toString(), QString("single"));
        QVERIFY(preferences.value("includeCompletionsForModuleExports").toBool());
        QCOMPARE(merged.settings, own.settings);
    }

    void editsBeforeThePrefixReplaceFromTheirStart()
    {
        // tsserver: "a." becomes "a?.name"; a scoped package replaces its "@".
        const QByteArray json = R"json([
            {"label": "name", "kind": 10, "insertText": "?.name",
             "textEdit": {"range": {"start": {"line": 1, "character": 1}, "end": {"line": 1, "character": 3}},
                          "newText": "?.name"}},
            {"label": "nap", "kind": 10,
             "textEdit": {"range": {"start": {"line": 1, "character": 2}, "end": {"line": 1, "character": 3}},
                          "newText": "nap"}}
        ])json";
        CompletionContext context;
        context.text = "x\na.n";
        context.position = 5;
        context.prefixStart = 4;
        context.prefix = "n";
        context.line = 1;
        context.column = 3;
        bool incomplete = false;
        const QList<CompletionItem> items =
            LspCompletionProvider::parse(QJsonDocument::fromJson(json).array(), &incomplete, nullptr, &context);
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].replaceStart, 3); // the dot
        QCOMPARE(items[0].insertText, QString("?.name"));
        QCOMPARE(items[1].replaceStart, -1); // the prefix itself
    }

    void answersRequestsOfTheFakeServer()
    {
        LspClient client(FAKE_LSP_SERVER_PATH, {}, QDir::tempPath());
        client.start();
        QJsonValue echoed;

        client.request("test/echo", QJsonObject{{"word", "total"}},
                       [&](const QJsonValue &result, const QString &) { echoed = result; });

        QTRY_COMPARE(echoed.toObject().value("word").toString(), QString("total"));
    }

    void cancelledRequestTellsTheServerAndSkipsItsHandler()
    {
        LspClient client(FAKE_LSP_SERVER_PATH, {}, QDir::tempPath());
        QSignalSpy ready(&client, &LspClient::ready);
        client.start();
        QVERIFY(ready.wait());
        QSignalSpy notifications(&client, &LspClient::notificationReceived);
        bool handled = false;
        const LspClient::RequestId id =
            client.request("test/silent", {}, [&](const QJsonValue &, const QString &) { handled = true; });

        client.cancel(id);

        QTRY_COMPARE(notifications.size(), 1);
        QCOMPARE(notifications[0][0].toString(), QString("test/cancelled"));
        QCOMPARE(notifications[0][1].toJsonValue().toObject().value("id").toInt(), id.value);
        QVERIFY(!handled);
    }

    void requestCancelledBeforeInitializationIsNeverSent()
    {
        LspClient client(FAKE_LSP_SERVER_PATH, {}, QDir::tempPath());
        const QSignalSpy notifications(&client, &LspClient::notificationReceived);
        const LspClient::RequestId id = client.request("test/silent", {}, [](const QJsonValue &, const QString &) { });
        client.cancel(id);
        QJsonValue echoed;

        client.start();
        client.request("test/echo", QJsonObject{{"n", 1}},
                       [&](const QJsonValue &result, const QString &) { echoed = result; });

        QTRY_VERIFY(echoed.isObject());
        QVERIFY(notifications.isEmpty()); // no "$/cancelRequest" either
    }

    void unansweredRequestFailsAfterItsTimeout()
    {
        LspClient client(FAKE_LSP_SERVER_PATH, {}, QDir::tempPath());
        client.start();
        // A request still waiting for the initialization is dropped, not cancelled.
        QTRY_VERIFY(client.isReady());
        const QSignalSpy notifications(&client, &LspClient::notificationReceived);
        QString failure;

        client.request(
            "test/silent", {}, [&](const QJsonValue &, const QString &error) { failure = error; },
            std::chrono::milliseconds(50));

        QTRY_VERIFY(!failure.isEmpty());
        QVERIFY2(failure.contains("No answer"), qPrintable(failure));
        QTRY_COMPARE(notifications.size(), 1); // and the server was told
    }

    void parsesProgressSteps()
    {
        using LspProtocol::Progress;
        const QJsonObject begin{{"token", 7},
                                {"value", QJsonObject{{"kind", "begin"}, {"title", "indexing"}, {"percentage", 0}}}};
        const QJsonObject report{{"token", "load"}, {"value", QJsonObject{{"kind", "report"}, {"message", "3/9"}}}};
        const QJsonObject partialResult{{"token", 1}, {"value", QJsonArray{}}};

        QCOMPARE(LspProtocol::parseProgress(begin),
                 (Progress{"7", Progress::Kind::Begin, "indexing", {}, std::optional<int>(0)}));
        QCOMPARE(LspProtocol::parseProgress(report), (Progress{"load", Progress::Kind::Report, {}, "3/9", {}}));
        QVERIFY(!LspProtocol::parseProgress(partialResult));
    }

    void tracksTheTasksServersReport()
    {
        const QTemporaryDir dir;
        Toolchains toolchains(dir.filePath("toolchains"), false);
        LanguageServers servers;
        servers.setFolder(dir.path());
        servers.setProject(projectRunningTheFakeServerAsClangd(dir.path(), toolchains), &toolchains);
        LspClient *client = servers.clientFor("cpp");
        QVERIFY(client);
        const QSignalSpy changes(&servers, &LanguageServers::tasksChanged);
        const QJsonArray started{QJsonObject{{"kind", "begin"}, {"title", "indexing"}},
                                 QJsonObject{{"kind", "report"}, {"percentage", 42}}};
        bool answered = false;

        client->request("test/progress", QJsonObject{{"token", "index"}, {"values", started}},
                        [&](const QJsonValue &, const QString &) { answered = true; });

        QTRY_VERIFY(answered);
        QCOMPARE(changes.size(), 2);
        QCOMPARE(servers.tasks().size(), 1);
        QCOMPARE(servers.tasks()[0].server, QString("clangd"));
        QCOMPARE(servers.tasks()[0].title, QString("indexing"));
        QCOMPARE(servers.tasks()[0].percentage, std::optional<int>(42));

        answered = false;
        client->request("test/progress",
                        QJsonObject{{"token", "index"}, {"values", QJsonArray{QJsonObject{{"kind", "end"}}}}},
                        [&](const QJsonValue &, const QString &) { answered = true; });

        QTRY_VERIFY(answered);
        QVERIFY(servers.tasks().isEmpty());
    }

    void closesADocumentInItsServer()
    {
        const QTemporaryDir dir;
        Toolchains toolchains(dir.filePath("toolchains"), false);
        LanguageServers servers;
        servers.setFolder(dir.path());
        servers.setProject(projectRunningTheFakeServerAsClangd(dir.path(), toolchains), &toolchains);
        LspClient *client = servers.clientFor("cpp");
        QVERIFY(client);
        const QString path = dir.filePath("a.cpp");
        servers.syncDocument(client, path, "cpp", "int a;\n");
        QSignalSpy notifications(client, &LspClient::notificationReceived);

        servers.closeDocument(path);

        QTRY_COMPARE(notifications.size(), 1);
        QCOMPARE(notifications[0][0].toString(), QString("test/didClose"));
        QVERIFY(servers.openDocuments().isEmpty());
    }

    void knowsWhetherATextIsTheVersionItsServerSaw()
    {
        const QTemporaryDir dir;
        Toolchains toolchains(dir.filePath("toolchains"), false);
        LanguageServers servers;
        servers.setFolder(dir.path());
        servers.setProject(projectRunningTheFakeServerAsClangd(dir.path(), toolchains), &toolchains);
        LspClient *client = servers.clientFor("cpp");
        QVERIFY(client);
        const QString path = dir.filePath("a.cpp");

        servers.syncDocument(client, path, "cpp", "int a;\n");
        servers.syncDocument(client, path, "cpp", "int b;\n");

        QVERIFY(servers.isCurrent(path, 2, "int b;\n"));
        QVERIFY(!servers.isCurrent(path, 1, "int b;\n"));
        QVERIFY(!servers.isCurrent(path, 2, "int c;\n"));
        QVERIFY(servers.isCurrent(dir.filePath("never-sent.cpp"), 7, "x"));
    }

    void describesAServerTask()
    {
        const LanguageServers::Task task{"clangd", "indexing", "3/9", 42};

        QCOMPARE(ServerTasksLabel::describe(task), QString("clangd: indexing 42% (3/9)"));
    }

    void usesTheProjectsServer()
    {
        const QTemporaryDir dir;
        Toolchains toolchains(dir.filePath("toolchains"), false);
        // A clangd downloaded by Kodamiko.
        const QString folder = toolchains.installFolder("clangd", "23.1.0");
        QDir().mkpath(folder + "/bin");
        QFile clangd(folder + "/" + Executables::fileName("bin/clangd"));
        QVERIFY(clangd.open(QIODevice::WriteOnly));
        clangd.close();
        QFile::setPermissions(clangd.fileName(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QFile marker(folder + "/.complete");
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.close();

        ProjectConfig project;
        project.path = dir.path();
        project.languages.append({"cpp", true, "c++20", {}, "clangd", "23.1.0"});
        project.languages.append({"java", false, "17", {}, "jdtls", "1.62.0"});
        LanguageServers servers;
        servers.setProject(project, &toolchains);
        QVERIFY(servers.specFor("cpp"));
        QCOMPARE(servers.specFor("c")->name, QString("clangd"));
        QVERIFY(servers.installed().contains("clangd 23.1.0"));
        // Turned off in the project: no server, even one in the PATH.
        QVERIFY(!servers.specFor("java"));
    }

    void readsJavaVersions()
    {
        QCOMPARE(JavaRuntimes::parseMajorVersion("1.8.0_504"), 8);
        QCOMPARE(JavaRuntimes::parseMajorVersion("25.0.3"), 25);
        QCOMPARE(JavaRuntimes::parseMajorVersion("21"), 21);
        QCOMPARE(JavaRuntimes::parseMajorVersion("garbage"), 0);
        // Whatever this machine has, newest first.
        const QList<JavaRuntime> runtimes = JavaRuntimes::find();
        for (int i = 1; i < runtimes.size(); ++i)
            QVERIFY(runtimes[i - 1].majorVersion >= runtimes[i].majorVersion);
    }

    void parsesCompletionItems()
    {
        const QByteArray json = R"json({"isIncomplete": true, "items": [
            {"label": " move(int dx)", "kind": 2, "detail": "void", "filterText": "move", "insertText": "move",
             "sortText": "b"},
            {"label": "•vector<class T>", "kind": 7, "filterText": "vector", "insertText": "vector", "sortText": "a"},
            {"label": "x", "kind": 5, "detail": "int", "textEdit": {"newText": "x"}},
            {"label": "equals", "kind": 2, "detail": "Object.equals(Object obj) : boolean",
             "labelDetails": {"detail": "(Object obj)", "description": "boolean"},
             "filterText": "equals(Object obj) : boolean", "insertText": "equals", "sortText": "c"}
        ]})json";
        bool incomplete = false;
        const QList<CompletionItem> items =
            LspCompletionProvider::parse(QJsonDocument::fromJson(json).object(), &incomplete);
        QVERIFY(incomplete);
        QCOMPARE(items.size(), 4);
        QCOMPARE(items[3].label, QString("equals")); // jdtls
        QCOMPARE(items[3].detail, QString("(Object obj)  boolean"));
        QCOMPARE(items[0].label, QString("x")); // no sortText sorts first
        QCOMPARE(items[1].label, QString("vector"));
        QCOMPARE(items[1].kind, CompletionItem::Kind::Class);
        QCOMPARE(items[2].label, QString("move"));
        QCOMPARE(items[2].kind, CompletionItem::Kind::Method);
        QCOMPARE(items[2].detail, QString("(int dx)  void"));
        QVERIFY(items[2].insertText.isEmpty()); // the label itself
        // The types, for smart completion.
        QCOMPARE(items[0].type, QString("int"));
        QCOMPARE(items[2].type, QString("void"));
        QCOMPARE(items[3].type, QString("boolean"));
    }

    void readsTheActiveParameterType()
    {
        const QByteArray json = R"json({"activeSignature": 0, "activeParameter": 1, "signatures": [
            {"label": "void take(int count, const char *name)",
             "parameters": [{"label": [10, 19]}, {"label": [21, 37]}]}]})json";
        QCOMPARE(LspCompletionProvider::activeParameterType(QJsonDocument::fromJson(json).object(), "cpp"),
                 QString("const char *"));
        const QByteArray ts = R"json({"signatures": [{"label": "f(size: number): void",
            "parameters": [{"label": "size: number"}]}], "activeParameter": 0})json";
        QCOMPARE(LspCompletionProvider::activeParameterType(QJsonDocument::fromJson(ts).object(), "typescript"),
                 QString("number"));
    }

    void smartCompletionWithClangd()
    {
        if (QStandardPaths::findExecutable("clangd").isEmpty())
            QSKIP("clangd is not installed");
        const QTemporaryDir dir;
        const QByteArray text = "struct Point {\n  int x;\n  const char *name;\n  void move(int dx);\n};\n"
                                "void take(const char *label);\n"
                                "int main() {\n  Point p;\n  take(p.\n}\n";
        QFile file(dir.filePath("a.cpp"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(text);
        file.close();

        LanguageServers servers;
        servers.setFolder(dir.path());
        LspCompletionProvider provider(&servers);
        CompletionContext context;
        context.filePath = dir.filePath("a.cpp");
        context.languageId = "cpp";
        context.text = text;
        context.position = int(text.indexOf("p.\n}") + 2);
        context.prefixStart = context.position;
        context.line = 8;
        context.column = 9;
        context.triggerCharacter = '.';
        context.mode = CompletionContext::Mode::Smart; // the type: from the signature help
        QStringList labels;
        bool answered = false;
        provider.complete(context, [&](const QList<CompletionItem> &received, bool) {
            for (const CompletionItem &item : received)
                labels.append(item.label);
            answered = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(answered, 30000);
        QCOMPARE(labels, QStringList({"name"}));
    }

    void completesMembersWithClangd()
    {
        if (QStandardPaths::findExecutable("clangd").isEmpty())
            QSKIP("clangd is not installed");
        const QTemporaryDir dir;
        const QByteArray text = "struct Point {\n  int x;\n  int y;\n  void move(int dx);\n};\n"
                                "int main() {\n  Point p;\n  p.\n}\n";
        QFile file(dir.filePath("a.cpp"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(text);
        file.close();

        LanguageServers servers;
        servers.setFolder(dir.path());
        LspCompletionProvider provider(&servers);
        QVERIFY(provider.triggerCharacters("cpp").contains('.'));

        CompletionContext context;
        context.filePath = dir.filePath("a.cpp");
        context.languageId = "cpp";
        context.text = text;
        context.position = int(text.indexOf("p.\n") + 2);
        context.prefixStart = context.position;
        context.line = 7;
        context.column = 4;
        context.triggerCharacter = '.';
        QVERIFY(provider.handles(context));

        QList<CompletionItem> items;
        bool answered = false;
        provider.complete(context, [&](const QList<CompletionItem> &received, bool) {
            items = received;
            answered = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(answered, 30000);
        QStringList labels;
        for (const CompletionItem &item : items)
            labels.append(item.label);
        QVERIFY2(labels.contains("x") && labels.contains("y") && labels.contains("move"),
                 qPrintable(labels.join(", ")));
        const auto move =
            std::find_if(items.begin(), items.end(), [](const CompletionItem &i) { return i.label == "move"; });
        QCOMPARE(move->kind, CompletionItem::Kind::Method);
        QVERIFY2(move->detail.contains("int dx"), qPrintable(move->detail));
        QVERIFY(move->isSnippet);
        QCOMPARE(move->insertText, QString("move(${1:int dx})"));
    }

    void completesMembersWithJdtls()
    {
        if (QStandardPaths::findExecutable("jdtls").isEmpty())
            QSKIP("jdtls is not installed");
        const QTemporaryDir dir;
        QDir().mkpath(dir.filePath("src"));
        const QByteArray text = "public class Main {\n"
                                "    int count;\n"
                                "    void increment(int step) { count += step; }\n"
                                "    public static void main(String[] args) {\n"
                                "        Main m = new Main();\n"
                                "        m.\n"
                                "    }\n"
                                "}\n";
        const QList<CompletionItem> items = completeAt(dir, {.name = "src/Main.java",
                                                             .languageId = "java",
                                                             .text = text,
                                                             .line = 5,
                                                             .column = 10,
                                                             .trigger = '.',
                                                             .timeoutMs = 120000});
        const QStringList labels = labelsOf(items);
        QVERIFY2(labels.contains("count") && labels.contains("increment"), qPrintable(labels.join(", ")));
        const CompletionItem increment = itemNamed(items, "increment");
        QVERIFY(increment.isSnippet);
        QVERIFY2(increment.insertText.startsWith("increment("), qPrintable(increment.insertText));
    }

    void jdtlsAddsImports()
    {
        if (QStandardPaths::findExecutable("jdtls").isEmpty())
            QSKIP("jdtls is not installed");
        const QTemporaryDir dir;
        QDir().mkpath(dir.filePath("src"));
        const QByteArray text = "public class Main {\n"
                                "    void f() {\n"
                                "        ArrayLi\n"
                                "    }\n"
                                "}\n";
        const QList<CompletionItem> items = completeAt(dir, {.name = "src/Main.java",
                                                             .languageId = "java",
                                                             .text = text,
                                                             .line = 2,
                                                             .column = 15,
                                                             .trigger = {},
                                                             .timeoutMs = 120000});
        const CompletionItem list = itemNamed(items, "ArrayList");
        QVERIFY2(!list.label.isEmpty(), qPrintable(labelsOf(items).join(", ")));
        QList<TextEdit> edits = list.additionalEdits;
        if (edits.isEmpty()) {
            QVERIFY(list.resolveEdits);
            bool answered = false;
            list.resolveEdits([&](const QList<TextEdit> &received) {
                edits = received;
                answered = true;
            });
            QTRY_VERIFY_WITH_TIMEOUT(answered, 30000);
        }
        QStringList texts;
        for (const TextEdit &edit : edits)
            texts.append(edit.newText);
        QVERIFY2(texts.join("").contains("import java.util.ArrayList;"), qPrintable(texts.join("|")));
    }

    void jdtlsCompletesAsIntelliJ()
    {
        if (QStandardPaths::findExecutable("jdtls").isEmpty())
            QSKIP("jdtls is not installed");
        const QTemporaryDir dir;
        QDir().mkpath(dir.filePath("src"));
        // Collectors' static members, offered without their class.
        const QByteArray text = "import java.util.List;\n"
                                "public class Main {\n"
                                "    Object f(List<String> names) {\n"
                                "        return names.stream().collect(toLi);\n"
                                "    }\n"
                                "}\n";
        const QList<CompletionItem> items = completeAt(dir, {.name = "src/Main.java",
                                                             .languageId = "java",
                                                             .text = text,
                                                             .line = 3,
                                                             .column = 42,
                                                             .trigger = {},
                                                             .timeoutMs = 120000});
        const CompletionItem toList = itemNamed(items, "toList");
        QVERIFY2(!toList.label.isEmpty(), qPrintable(labelsOf(items).join(", ")));
        QVERIFY2(toList.detail.contains("Collector<"), qPrintable(toList.detail));
        // Its own postfix templates are off: Kodamiko's are offered instead.
        const QByteArray member = "public class Main {\n"
                                  "    void f(String name) {\n"
                                  "        name.\n"
                                  "    }\n"
                                  "}\n";
        const QList<CompletionItem> members = completeAt(dir, {.name = "src/Main.java",
                                                               .languageId = "java",
                                                               .text = member,
                                                               .line = 2,
                                                               .column = 13,
                                                               .trigger = '.',
                                                               .timeoutMs = 120000});
        QVERIFY2(itemNamed(members, "length").kind == CompletionItem::Kind::Method,
                 qPrintable(labelsOf(members).join(", ")));
        for (const CompletionItem &item : members)
            QVERIFY2(item.kind != CompletionItem::Kind::Snippet, qPrintable(item.label));
    }

private:
    static QStringList labelsOf(const QList<CompletionItem> &items)
    {
        QStringList labels;
        for (const CompletionItem &item : items)
            labels.append(item.label);
        return labels;
    }

    static CompletionItem itemNamed(const QList<CompletionItem> &items, const QString &label)
    {
        for (const CompletionItem &item : items) {
            if (item.label == label)
                return item;
        }
        return {};
    }

    // What completeAt asks the language server.
    struct CompletionRequest {
        QString name;       // the file, saved in the test's directory
        QString languageId; // the server to ask
        QByteArray text;
        int line = 0;
        int column = 0;
        QChar trigger;
        int timeoutMs = 0;
    };

    // Candidates the language server of `request.languageId` proposes at
    // (line, column) of `request.text`, saved as `request.name` in `dir`.
    // The servers stay alive with the test object (resolving needs them).
    QList<CompletionItem> completeAt(const QTemporaryDir &dir, const CompletionRequest &request)
    {
        QFile file(dir.filePath(request.name));
        if (!file.open(QIODevice::WriteOnly))
            return {};
        file.write(request.text);
        file.close();

        servers_ = std::make_unique<LanguageServers>();
        LanguageServers &servers = *servers_;
        servers.setFolder(dir.path());
        LspCompletionProvider provider(&servers);
        CompletionContext context;
        context.filePath = dir.filePath(request.name);
        context.languageId = request.languageId;
        context.text = request.text;
        context.line = request.line;
        context.column = request.column;
        context.triggerCharacter = request.trigger;

        // A server still loading the project may answer with nothing: ask
        // again until something comes or the time is up.
        QElapsedTimer elapsed;
        elapsed.start();
        QList<CompletionItem> found;
        while (found.isEmpty() && elapsed.elapsed() < request.timeoutMs) {
            bool answered = false;
            provider.complete(context, [&](const QList<CompletionItem> &items, bool) {
                found = items;
                answered = true;
            });
            while (!answered && elapsed.elapsed() < request.timeoutMs)
                QTest::qWait(50);
            if (found.isEmpty())
                QTest::qWait(1000);
        }
        return found;
    }

    std::unique_ptr<LanguageServers> servers_;
};

QTEST_GUILESS_MAIN(TestLsp)
#include "tst_lsp.moc"
