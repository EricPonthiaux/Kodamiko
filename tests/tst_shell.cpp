// SPDX-License-Identifier: GPL-3.0-only

#include "system/Shell.h"

#include <QProcess>
#include <QtTest>

namespace {

struct Run {
    int exitCode = -1;
    QByteArray output;
};

Run run(const ShellCommand &command)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(command.program, command.arguments);
    if (!process.waitForFinished(20000))
        return {};
    return {.exitCode = process.exitCode(), .output = process.readAll()};
}

} // namespace

// What every system's shells do; tst_shell_windows.cpp and
// tst_shell_unix.cpp check which shell each system uses.
class TestShell : public QObject {
    Q_OBJECT

private slots:

    void theSystemShellGivesTheCommandsOutput()
    {
        QVERIFY(run(Shell::systemShellRunning("echo kodamiko")).output.contains("kodamiko"));
    }

    void theSystemShellGivesTheCommandsExitCode() { QCOMPARE(run(Shell::systemShellRunning("exit 3")).exitCode, 3); }

    void theLoginShellGivesTheCommandsExitCode() { QCOMPARE(run(Shell::loginShellRunning("exit 3")).exitCode, 3); }

    void theUsersShellRunsTheCommandThenExits()
    {
        // The same in sh and PowerShell.
        const Run result = run(Shell::userShellRunning("echo hello; exit 3"));

        QVERIFY(result.output.contains("hello"));
        QCOMPARE(result.exitCode, 3);
    }

    void aVerbatimCommandRuns()
    {
        QProcess process;
        Shell::prepareVerbatim(process, "echo kodamiko");

        process.start();

        QVERIFY(process.waitForFinished(20000));
        QVERIFY(process.readAllStandardOutput().contains("kodamiko"));
    }

    void anExecutableIsLaunchedAsItIs()
    {
        const ShellCommand command = Shell::launchable("/usr/bin/claude", {"--print"});

        QCOMPARE(command.program, QString("/usr/bin/claude"));
        QCOMPARE(command.arguments, QStringList{"--print"});
    }
};

QTEST_GUILESS_MAIN(TestShell)
#include "tst_shell.moc"
