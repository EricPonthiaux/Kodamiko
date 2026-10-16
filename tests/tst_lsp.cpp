#include "lsp/LanguageServers.h"
#include "lsp/LspClient.h"
#include "lsp/LspCompletionProvider.h"
#include "toolchains/JavaRuntimes.h"
#include "toolchains/Toolchains.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

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
        LanguageSetup cpp{"cpp", true, "c++23", {}, "clangd", "23.1.0"};
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

        LanguageSetup python{"python", true, "3.12", "/p/.venv/bin/python", "pyright", "1.1.414"};
        QCOMPARE(LspClient::settingAt(LanguageServers::optionsFor("pyright", python, "/p/.venv/bin", "/p/.venv/bin/python")
                                          .settings,
                                      "python.pythonPath")
                     .toString(),
                 QString("/p/.venv/bin/python"));
    }

    void usesTheProjectsServer()
    {
        QTemporaryDir dir;
        Toolchains toolchains(dir.filePath("toolchains"), false);
        // A clangd downloaded by Duet.
        const QString folder = toolchains.installFolder("clangd", "23.1.0");
        QDir().mkpath(folder + "/bin");
        QFile clangd(folder + "/bin/clangd");
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
        const QList<CompletionItem> items = LspCompletionProvider::parse(QJsonDocument::fromJson(json).object(),
                                                                          &incomplete);
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
    }

    void completesMembersWithClangd()
    {
        if (QStandardPaths::findExecutable("clangd").isEmpty())
            QSKIP("clangd is not installed");
        QTemporaryDir dir;
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
        QVERIFY2(labels.contains("x") && labels.contains("y") && labels.contains("move"), qPrintable(labels.join(", ")));
        const auto move = std::find_if(items.begin(), items.end(), [](const CompletionItem &i) { return i.label == "move"; });
        QCOMPARE(move->kind, CompletionItem::Kind::Method);
        QVERIFY2(move->detail.contains("int dx"), qPrintable(move->detail));
        QVERIFY(move->isSnippet);
        QCOMPARE(move->insertText, QString("move(${1:int dx})"));
    }

    void completesMembersWithJdtls()
    {
        if (QStandardPaths::findExecutable("jdtls").isEmpty())
            QSKIP("jdtls is not installed");
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath("src"));
        const QByteArray text = "public class Main {\n"
                                "    int count;\n"
                                "    void increment(int step) { count += step; }\n"
                                "    public static void main(String[] args) {\n"
                                "        Main m = new Main();\n"
                                "        m.\n"
                                "    }\n"
                                "}\n";
        const QList<CompletionItem> items = completeAt(dir, "src/Main.java", "java", text, 5, 10, '.', 120000);
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
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath("src"));
        const QByteArray text = "public class Main {\n"
                                "    void f() {\n"
                                "        ArrayLi\n"
                                "    }\n"
                                "}\n";
        const QList<CompletionItem> items = completeAt(dir, "src/Main.java", "java", text, 2, 15, {}, 120000);
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

    // Candidates the language server of `languageId` proposes at (line,
    // column) of `text`, saved as `name` in `dir`. The servers stay alive
    // with the test object (resolving needs them).
    QList<CompletionItem> completeAt(const QTemporaryDir &dir, const QString &name, const QString &languageId,
                                  const QByteArray &text, int line, int column, QChar trigger, int timeoutMs)
    {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::WriteOnly))
            return {};
        file.write(text);
        file.close();

        servers_ = std::make_unique<LanguageServers>();
        LanguageServers &servers = *servers_;
        servers.setFolder(dir.path());
        LspCompletionProvider provider(&servers);
        CompletionContext context;
        context.filePath = dir.filePath(name);
        context.languageId = languageId;
        context.text = text;
        context.line = line;
        context.column = column;
        context.triggerCharacter = trigger;

        // A server still loading the project may answer with nothing: ask
        // again until something comes or the time is up.
        QElapsedTimer elapsed;
        elapsed.start();
        QList<CompletionItem> found;
        while (found.isEmpty() && elapsed.elapsed() < timeoutMs) {
            bool answered = false;
            provider.complete(context, [&](const QList<CompletionItem> &items, bool) {
                found = items;
                answered = true;
            });
            while (!answered && elapsed.elapsed() < timeoutMs)
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
