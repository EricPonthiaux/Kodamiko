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

// Runs in a login shell of the Unixes, sh syntax.
class TestRunUnix : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // Settings of the tests, not the user's.
        QCoreApplication::setOrganizationName(QStringLiteral("DuetTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_run_unix"));
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
        configuration.command = "printf 'main.c:1:5: error: oops\\n'; echo \"$GREETING\" > out.txt; exit 3";
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

    void stopsARun()
    {
        const QTemporaryDir dir;
        ProjectConfig project;
        project.path = dir.path();
        ProblemStore store(nullptr);
        Runner runner(&store);
        runner.setProject(project, nullptr);
        RunConfiguration configuration;
        configuration.name = "Serve";
        configuration.command = "trap '' INT; echo ready; sleep 30";
        TerminalWidget terminal;
        QByteArray output;
        connect(&terminal, &TerminalWidget::outputReceived, this,
                [&output](const QByteArray &data) { output += data; });
        QSignalSpy finished(&runner, &Runner::finished);
        runner.start(configuration, &terminal);
        QTRY_VERIFY_WITH_TIMEOUT(output.contains("ready"), 10000); // the trap is set
        QVERIFY(runner.isRunning("Serve"));
        QCOMPARE(runner.running(), QStringList{"Serve"});
        runner.stop("Serve"); // Ctrl+C, which it ignores
        QTest::qWait(300);
        QVERIFY(runner.isRunning("Serve"));
        runner.stop("Serve"); // then ended
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(1).toInt(), -1);
        QVERIFY(!runner.isRunning("Serve"));
    }
};

QTEST_MAIN(TestRunUnix)
#include "tst_run_unix.moc"
