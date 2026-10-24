// SPDX-License-Identifier: GPL-3.0-only

#include "system/Shell.h"

#include <QFileInfo>
#include <QProcess>
#include <QtTest>

// The shells of the Unixes: the user's $SHELL, /bin/sh for commands.
class TestShellUnix : public QObject {
    Q_OBJECT

private slots:

    void init() { qputenv("SHELL", "/bin/zsh"); }

    void cleanup()
    {
        if (shell_.isNull())
            qunsetenv("SHELL");
        else
            qputenv("SHELL", shell_);
    }

    void theUsersShellIsALoginShell()
    {
        const ShellCommand shell = Shell::userShell();

        QCOMPARE(shell.program, QString("/bin/zsh"));
        QCOMPARE(shell.arguments, QStringList{"-l"});
    }

    void theUsersShellRunsACommandWithItsCommandOption()
    {
        const ShellCommand shell = Shell::userShellRunning("ls");

        QCOMPARE(shell.program, QString("/bin/zsh"));
        QCOMPARE(shell.arguments, (QStringList{"-c", "ls"}));
    }

    void theLoginShellReadsTheUsersProfile()
    {
        const ShellCommand shell = Shell::loginShellRunning("make");

        QCOMPARE(shell.program, QString("/bin/zsh"));
        QCOMPARE(shell.arguments, (QStringList{"-l", "-c", "make"}));
    }

    void withoutShellVariableTheUsersShellIsSh()
    {
        qunsetenv("SHELL");

        QCOMPARE(Shell::userShell().program, QString("/bin/sh"));
    }

    void theSystemShellIsSh()
    {
        const ShellCommand shell = Shell::systemShellRunning("make check");

        QCOMPARE(shell.program, QString("/bin/sh"));
        QCOMPARE(shell.arguments, (QStringList{"-c", "make check"}));
    }

    void aScriptIsLaunchedAsItIs()
    {
        const ShellCommand command = Shell::launchable("/opt/npm/claude.cmd", {"--print"});

        QCOMPARE(command.program, QString("/opt/npm/claude.cmd"));
    }

    void aProgramOfTheWorkingFolderIsNamedFromIt()
    {
        QCOMPARE(Shell::programInWorkingFolder("mvnw"), QString("./mvnw"));
    }

    void aVerbatimCommandRunsInBash()
    {
        QProcess process;

        Shell::prepareVerbatim(process, "echo \"a b\"");

        const QString name = QFileInfo(process.program()).fileName();
        QVERIFY2(name == "bash" || process.program() == "/bin/sh", qPrintable(process.program()));
        QCOMPARE(process.arguments(), (QStringList{"-c", "echo \"a b\""}));
    }

private:
    const QByteArray shell_ = qgetenv("SHELL");
};

QTEST_GUILESS_MAIN(TestShellUnix)
#include "tst_shell_unix.moc"
