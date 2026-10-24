// SPDX-License-Identifier: GPL-3.0-only

#include "system/Shell.h"

#include <QFileInfo>
#include <QProcess>
#include <QtTest>

// The shells of Windows: PowerShell for the user, cmd.exe for commands.
class TestShellWindows : public QObject {
    Q_OBJECT

private slots:

    void theUsersShellIsPowerShell()
    {
        const ShellCommand shell = Shell::userShell();

        const QString name = QFileInfo(shell.program).fileName().toLower();
        QVERIFY2(name == "pwsh.exe" || name == "powershell.exe", qPrintable(shell.program));
        QCOMPARE(shell.arguments, QStringList{"-NoLogo"});
    }

    void theUsersShellRunsACommandWithItsCommandOption()
    {
        QCOMPARE(Shell::userShellRunning("dir").arguments, (QStringList{"-NoLogo", "-Command", "dir"}));
    }

    void theLoginShellIsTheCommandPromptWithoutItsAutoRun()
    {
        const ShellCommand shell = Shell::loginShellRunning("mvnw test");

        QCOMPARE(shell.program, qEnvironmentVariable("ComSpec", "cmd.exe"));
        QCOMPARE(shell.arguments, (QStringList{"/d", "/s", "/c", "mvnw test"}));
    }

    void theSystemShellIsCmd()
    {
        const ShellCommand shell = Shell::systemShellRunning("ctest");

        QCOMPARE(shell.program, QString("cmd.exe"));
        QCOMPARE(shell.arguments, (QStringList{"/c", "ctest"}));
    }

    void aBatchFileIsLaunchedThroughCmd()
    {
        const ShellCommand command = Shell::launchable("C:/npm/claude.cmd", {"--print"});

        QCOMPARE(command.program, QString("cmd.exe"));
        QCOMPARE(command.arguments, (QStringList{"/c", "C:\\npm\\claude.cmd", "--print"}));
    }

    void aProgramOfTheWorkingFolderIsNamedAlone() { QCOMPARE(Shell::programInWorkingFolder("mvnw"), QString("mvnw")); }

    void aVerbatimCommandKeepsItsQuotes()
    {
        QProcess process;

        Shell::prepareVerbatim(process, "echo \"a b\"");

        QCOMPARE(process.program(), QString("cmd.exe"));
        QCOMPARE(process.nativeArguments(), QString("/c echo \"a b\""));
    }
};

QTEST_GUILESS_MAIN(TestShellWindows)
#include "tst_shell_windows.moc"
