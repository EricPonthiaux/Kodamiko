// SPDX-License-Identifier: GPL-3.0-only

#include "diagnostics/ProblemStore.h"
#include "run/BuildOutputParser.h"
#include "run/RunConfiguration.h"
#include "run/Runner.h"
#include "system/Shell.h"
#include "terminal/TerminalWidget.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

namespace {

void write(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QStringList names(const QList<RunConfiguration> &configurations)
{
    QStringList list;
    for (const RunConfiguration &configuration : configurations)
        list.append(configuration.name);
    return list;
}

const RunConfiguration *named(const QList<RunConfiguration> &configurations, const QString &name)
{
    for (const RunConfiguration &configuration : configurations) {
        if (configuration.name == name)
            return &configuration;
    }
    return nullptr;
}

Problems parse(const QString &folder, const QByteArray &output)
{
    BuildOutputParser parser(folder, folder);
    parser.feed(output);
    parser.finish();
    return parser.problems();
}

} // namespace

class TestRun : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("DuetTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_run"));
        QSettings().clear();
    }

    void cleanup() { QSettings().clear(); }

    void offersTheRecipeConfigurationsAfterTheProjectsOwn()
    {
        const QTemporaryDir dir;
        write(dir.path() + "/.duet/run.json",
              R"({"configurations": [{"name": "Build", "kind": "build", "command": "make"}]})");
        ProjectConfig project;
        project.path = dir.path();
        ProblemStore store(nullptr);
        Runner runner(&store);
        runner.setProject(project, nullptr);

        RunConfiguration build;
        build.name = "Build";
        build.kind = RunConfiguration::Kind::Build;
        build.command = "cmake";
        RunConfiguration run;
        run.name = "Run";
        run.command = "app";

        runner.setRecipeConfigurations({build, run});

        const QList<RunConfiguration> all = runner.configurations();
        QCOMPARE(all.size(), 2);
        QCOMPARE(all.at(0).command, QString("make"));
        QCOMPARE(all.at(1).name, QString("Run"));
    }

    void readsAndWritesRunJson()
    {
        RunConfiguration build;
        build.name = "Build";
        build.kind = RunConfiguration::Kind::Build;
        build.command = "cmake --build build";
        build.folder = "app";
        build.environment.insert("VERBOSE", "1");
        RunConfiguration test;
        test.name = "Tests";
        test.kind = RunConfiguration::Kind::Test;
        test.command = "ctest";
        RunConfiguration debug;
        debug.name = "Debug";
        debug.kind = RunConfiguration::Kind::Debug;
        debug.command = "gdb build/app";
        const QList<RunConfiguration> configurations{build, test, debug};
        QCOMPARE(RunConfigurations::parse(RunConfigurations::serialize(configurations)), configurations);

        // Entries without a name or a command are skipped; an unknown kind runs.
        const QList<RunConfiguration> parsed = RunConfigurations::parse(
            R"({"configurations": [{"name": "A", "command": "a", "kind": "deploy"}, {"name": "B"}, {"command": "c"}]})");
        QCOMPARE(names(parsed), QStringList{"A"});
        QCOMPARE(parsed.first().kind, RunConfiguration::Kind::Run);

        QString error;
        QVERIFY(RunConfigurations::parse("{ nope", &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void detectsCMakePresets()
    {
        const QTemporaryDir dir;
        write(dir.path() + "/CMakeLists.txt", "project(x)\n");
        write(dir.path() + "/CMakePresets.json", R"({
            "version": 3,
            "configurePresets": [{"name": "base", "hidden": true}, {"name": "default", "inherits": "base"}],
            "buildPresets": [{"name": "default", "configurePreset": "default"}],
            "testPresets": [{"name": "default", "configurePreset": "default"}]
        })");
        const QList<RunConfiguration> found = RunConfigurations::detect(dir.path());
        QCOMPARE(names(found), (QStringList{"CMake: build default", "CMake: configure default", "CTest: default"}));
        QCOMPARE(found.at(0).command, QString("cmake --build --preset default"));
        QCOMPARE(found.at(2).kind, RunConfiguration::Kind::Test);
        QVERIFY(found.at(0).detected);
    }

    void detectsCMakeWithoutPresets()
    {
        const QTemporaryDir dir;
        write(dir.path() + "/CMakeLists.txt", "project(x)\n");
        const QList<RunConfiguration> found = RunConfigurations::detect(dir.path());
        QCOMPARE(names(found), (QStringList{"CMake: build", "CTest"}));
        QCOMPARE(found.first().kind, RunConfiguration::Kind::Build);
    }

    void detectsPackageScripts()
    {
        const QTemporaryDir dir;
        write(dir.path() + "/package.json", R"({"scripts": {"dev": "vite", "build": "vite build",
            "prebuild": "rimraf dist", "test": "vitest", "lint": "eslint ."}})");
        write(dir.path() + "/pnpm-lock.yaml", "");
        const QList<RunConfiguration> found = RunConfigurations::detect(dir.path());
        // Builds first; npm's pre- hooks are not offered on their own.
        QCOMPARE(names(found), (QStringList{"pnpm: build", "pnpm: lint", "pnpm: dev", "pnpm: test"}));
        QCOMPARE(named(found, "pnpm: dev")->command, QString("pnpm run dev"));
        QCOMPARE(named(found, "pnpm: dev")->kind, RunConfiguration::Kind::Run);
        QCOMPARE(named(found, "pnpm: test")->kind, RunConfiguration::Kind::Test);
    }

    void detectsJavaBuilds()
    {
        const QTemporaryDir dir;
        write(dir.path() + "/pom.xml", "<project/>");
        write(dir.path() + "/mvnw", "#!/bin/sh\n");
        const QList<RunConfiguration> found = RunConfigurations::detect(dir.path());
        QVERIFY(named(found, "Maven: compile"));
        QCOMPARE(named(found, "Maven: test")->command, Shell::programInWorkingFolder("mvnw") + " test");
    }

    void detectsMakeTargets()
    {
        const QTemporaryDir dir;
        write(dir.path() + "/Makefile", "CC := gcc\nall: main\n\ntest: all\n\t./run-tests\n");
        QCOMPARE(names(RunConfigurations::detect(dir.path())), (QStringList{"make", "make test"}));
    }

    void runJsonReplacesDetectedConfigurations()
    {
        const QTemporaryDir dir;
        write(dir.path() + "/CMakeLists.txt", "project(x)\n");
        write(dir.path() + "/.duet/run.json",
              R"({"configurations": [{"name": "CTest", "kind": "test", "command": "ctest -j8"},
                                     {"name": "Serve", "command": "./serve"}]})");
        const QList<RunConfiguration> all = RunConfigurations::all(dir.path());
        QCOMPARE(names(all), (QStringList{"CTest", "Serve", "CMake: build"}));
        QCOMPARE(all.first().command, QString("ctest -j8"));
        QVERIFY(!all.first().detected);

        write(dir.path() + "/.duet/run.json", "{");
        QString error;
        QCOMPARE(names(RunConfigurations::all(dir.path(), &error)), (QStringList{"CMake: build", "CTest"}));
        QVERIFY(error.startsWith(".duet/run.json"));
    }

    void cleansTerminalOutput()
    {
        QCOMPARE(BuildOutputParser::cleanLine("\x1b[1m\x1b[31merror\x1b[0m: x\r"), QString("error: x"));
        QCOMPARE(BuildOutputParser::cleanLine("\x1b]0;title\x07[1/3] a\r\x1b[K[2/3] b"), QString("[2/3] b"));
        QCOMPARE(BuildOutputParser::cleanLine("\x1b]8;;file:///a\x1b\\link\x1b]8;;\x1b\\"), QString("link"));
    }

    void readsCompilerErrors()
    {
        const QTemporaryDir dir;
        write(dir.path() + "/src/main.cpp", "int main() {}\n");
        write(dir.path() + "/src/App.java", "class App {}\n");
        write(dir.path() + "/web/a.ts", "let a = 1;\n");

        const Problems problems =
            parse(dir.path(),
                  "[ 50%] Building CXX object main.cpp.o\n"
                  "\x1b[1msrc/main.cpp:12:5: \x1b[31merror: \x1b[0m'x' was not declared in this scope\r\n"
                  "src/main.cpp:3:1: warning: unused variable 'y' [-Wunused-variable]\n"
                  "src/main.cpp:3:1: note: declared here\n"
                  "In file included from src/main.cpp:2:\n"
                  "src/main.cpp:12:5: error: 'x' was not declared in this scope\n" // told twice
                      + dir.path().toUtf8() +
                      "/src/App.java:7: error: cannot find symbol\n"
                      "[ERROR] " +
                      dir.path().toUtf8() +
                      "/src/App.java:[9,13] incompatible types\n"
                      "web/a.ts(4,7): error TS2322: Type 'string' is not assignable to type 'number'.\n"
                      "web/a.ts:5:1 - error TS2304: Cannot find name 'b'.\n"
                      "src/main.cpp(20): warning C4996: 'strcpy': This function may be unsafe.\n"
                      "missing.cpp:1:1: error: a file that is not there\n"
                      "make: *** [Makefile:12: all] Error 1\n");
        QCOMPARE(problems.size(), 7);
        QCOMPARE(problems.at(0).path, dir.path() + "/src/main.cpp");
        QCOMPARE(problems.at(0).line, 11);
        QCOMPARE(problems.at(0).column, 4);
        QCOMPARE(problems.at(0).severity, Diagnostic::Severity::Error);
        QCOMPARE(problems.at(0).message, QString("'x' was not declared in this scope"));
        QCOMPARE(problems.at(1).severity, Diagnostic::Severity::Warning);
        QCOMPARE(problems.at(2).path, dir.path() + "/src/App.java");
        QCOMPARE(problems.at(2).column, -1);
        QCOMPARE(problems.at(3).line, 8);
        QCOMPARE(problems.at(3).column, 12);
        QCOMPARE(problems.at(4).message, QString("Type 'string' is not assignable to type 'number'."));
        QCOMPARE(problems.at(5).line, 4);
        QCOMPARE(problems.at(6).severity, Diagnostic::Severity::Warning);
        QCOMPARE(problems.at(6).line, 19);
    }

    void readsMultiLineProblems()
    {
        const QTemporaryDir dir;
        write(dir.path() + "/src/main.rs", "fn main() {}\n");
        write(dir.path() + "/CMakeLists.txt", "project(x)\n");
        write(dir.path() + "/app/run.py", "main()\n");
        write(dir.path() + "/app/A.kt", "fun a() {}\n");
        write(dir.path() + "/web/b.js", "b()\n");

        const QByteArray root = dir.path().toUtf8();
        const Problems problems =
            parse(dir.path(), "error[E0308]: mismatched types\n"
                              " --> src/main.rs:4:18\n"
                              "  |\n"
                              "CMake Error at CMakeLists.txt:5 (add_executable):\n"
                              "  Cannot find source file:\n"
                              "\n"
                              "    missing.cpp\n"
                              "\n"
                              "Call Stack (most recent call first):\n"
                              "Traceback (most recent call last):\n"
                              "  File \"" +
                                  root +
                                  "/app/run.py\", line 1, in <module>\n"
                                  "    main()\n"
                                  "  File \"/usr/lib/python3/json/__init__.py\", line 7, in loads\n"
                                  "ValueError: bad value\n"
                                  "e: file://" +
                                  root + "/app/A.kt:3:9 Unresolved reference: x\n" + root +
                                  "/web/b.js\n"
                                  "  2:3  warning  'b' is not defined  no-undef\n"
                                  "\n");
        QCOMPARE(problems.size(), 5);
        QCOMPARE(problems.at(0).path, dir.path() + "/src/main.rs");
        QCOMPARE(problems.at(0).line, 3);
        QCOMPARE(problems.at(0).message, QString("mismatched types"));
        QCOMPARE(problems.at(1).path, dir.path() + "/CMakeLists.txt");
        QCOMPARE(problems.at(1).line, 4);
        QCOMPARE(problems.at(1).message, QString("Cannot find source file: missing.cpp"));
        // The deepest frame in the project, not the library's.
        QCOMPARE(problems.at(2).path, dir.path() + "/app/run.py");
        QCOMPARE(problems.at(2).message, QString("ValueError: bad value"));
        QCOMPARE(problems.at(3).path, dir.path() + "/app/A.kt");
        QCOMPARE(problems.at(3).column, 8);
        QCOMPARE(problems.at(4).path, dir.path() + "/web/b.js");
        QCOMPARE(problems.at(4).severity, Diagnostic::Severity::Warning);
        QCOMPARE(problems.at(4).message, QString("'b' is not defined"));
    }

    void readsOutputInPieces()
    {
        const QTemporaryDir dir;
        write(dir.path() + "/é.c", "int x;\n");
        BuildOutputParser parser(dir.path(), dir.path());
        const QByteArray output = "\xc3\xa9.c:2:3: error: split\n\xc3\xa9.c:4:1: warning: last line";
        for (const char byte : output)
            parser.feed(QByteArrayView(&byte, 1)); // UTF-8 cut in the middle too
        QCOMPARE(parser.problems().size(), 1);
        parser.finish();
        QCOMPARE(parser.problems().size(), 2);
        QCOMPARE(parser.problems().at(0).path, dir.path() + QString::fromUtf8("/é.c"));
    }

    void keepsBuildProblemsBySource()
    {
        ProblemStore store(nullptr);
        const QSignalSpy changed(&store, &ProblemStore::changed);
        Problem a;
        a.path = "/p/a.cpp";
        a.line = 3;
        a.message = "a";
        Problem b = a;
        b.path = "/p/b.cpp";
        store.setBuildProblems("Build", {a, b});
        store.setBuildProblems("Tests", {a});
        QCOMPARE(store.files(), (QStringList{"/p/a.cpp", "/p/b.cpp"}));
        QCOMPARE(store.problems("/p/a.cpp").size(), 2);
        QCOMPARE(store.problems("/p/a.cpp").first().source, QString("Build"));
        QCOMPARE(changed.size(), 3);

        // The next run of Build replaces its problems only.
        store.setBuildProblems("Build", {});
        QCOMPARE(store.files(), QStringList{"/p/a.cpp"});
        QCOMPARE(store.problems("/p/a.cpp").first().source, QString("Tests"));

        store.clearBuildProblems();
        QVERIFY(store.files().isEmpty());
    }

    void remembersTheChosenConfiguration()
    {
        const QTemporaryDir dir;
        write(dir.path() + "/Cargo.toml", "[package]\n");
        ProjectConfig project;
        project.path = dir.path();
        ProblemStore store(nullptr);
        Runner runner(&store);
        runner.setProject(project, nullptr);
        QCOMPARE(runner.selected()->name, QString("Cargo: build"));
        runner.select("Cargo: test");
        QCOMPARE(runner.selected()->name, QString("Cargo: test"));
        // Ctrl+F9 builds even when tests are chosen.
        QCOMPARE(runner.buildConfiguration()->name, QString("Cargo: build"));

        Runner again(&store);
        again.setProject(project, nullptr);
        QCOMPARE(again.selected()->name, QString("Cargo: test"));
        runner.select("gone");
        QCOMPARE(runner.selected()->name, QString("Cargo: build"));
    }
};

QTEST_MAIN(TestRun)
#include "tst_run.moc"
