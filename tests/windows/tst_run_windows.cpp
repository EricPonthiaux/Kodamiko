// SPDX-License-Identifier: GPL-3.0-only

#include "diagnostics/ProblemStore.h"
#include "projects/ProjectConfig.h"
#include "run/RunConfiguration.h"
#include "run/Runner.h"
#include "terminal/TerminalWidget.h"

#include <QDir>
#include <QFile>
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

} // namespace

// Runs in cmd.exe, its syntax.
class TestRunWindows : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // Settings of the tests, not the user's.
        QCoreApplication::setOrganizationName(QStringLiteral("KDKTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_run_windows"));
    }

    void runsAConfigurationAndReadsItsProblems()
    {
        const QTemporaryDir dir;
        write(dir.path() + "/main.c", "int main() {}\n");
        ProjectConfig project;
        project.path = dir.path();
        ProblemStore store(nullptr);
        Runner runner(&store);
        runner.setProject(project, nullptr);

        RunConfiguration configuration;
        configuration.name = "Build";
        configuration.kind = RunConfiguration::Kind::Build;
        configuration.command = "echo main.c:1:5: error: oops& echo %GREETING%> out.txt& exit 3";
        configuration.environment.insert("GREETING", "hello");
        const ProcessSpec spec = runner.processSpec(configuration);
        QCOMPARE(spec.workingDirectory, dir.path());
        QCOMPARE(spec.environment.value("GREETING"), QString("hello"));

        // In a tab not shown, as builds run in Edit mode.
        QTabWidget tabs;
        tabs.resize(800, 400);
        tabs.addTab(new QWidget, "other");
        tabs.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tabs));
        auto *terminalPtr = new TerminalWidget(&tabs);
        tabs.addTab(terminalPtr, "Build");
        TerminalWidget &terminal = *terminalPtr;
        QSignalSpy finished(&runner, &Runner::finished);
        runner.start(configuration, &terminal);
        QVERIFY(runner.isRunning("Build") || !finished.isEmpty());
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.first().at(1).toInt(), 3);
        QCOMPARE(finished.first().at(2).toInt(), 1); // errors
        const Problems problems = store.problems(dir.path() + "/main.c");
        QCOMPARE(problems.size(), 1);
        QCOMPARE(problems.first().message, QString("oops"));
        QCOMPARE(problems.first().source, QString("Build"));
        QFile out(dir.path() + "/out.txt");
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(out.readAll().trimmed(), QByteArray("hello"));
        QVERIFY(runner.running().isEmpty());
        // The output stays in the terminal, never shown yet.
        QVERIFY(terminal.lastLines(10).join('\n').contains("main.c:1:5: error: oops"));
        tabs.setCurrentIndex(1);
        QVERIFY(terminal.lastLines(10).join('\n').contains("main.c:1:5: error: oops"));
    }
};

QTEST_MAIN(TestRunWindows)
#include "tst_run_windows.moc"
