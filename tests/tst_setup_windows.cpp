// SPDX-License-Identifier: GPL-3.0-only

#include "setup/Prerequisites.h"

#include <QtTest>

// The installs on Windows: PowerShell scripts, winget.
class TestSetupWindows : public QObject {
    Q_OBJECT

private slots:

    void installRunsInPowerShellDespiteItsPolicy()
    {
        Prerequisite claude;
        claude.installCommand = "irm https://claude.ai/install.ps1 | iex";

        const ProcessSpec spec = Prerequisites::installProcess(claude, "C:/Users/me");

        QCOMPARE(spec.program, QString("powershell.exe"));
        QCOMPARE(spec.arguments,
                 (QStringList{"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", claude.installCommand}));
    }

    void updateCallsTheProgramFound()
    {
        Prerequisite claude;
        claude.installCommand = "claude update";
        claude.path = "C:/Users/me/.local/bin/claude.exe";

        const ProcessSpec spec = Prerequisites::installProcess(claude, "C:/Users/me");

        QCOMPARE(spec.arguments.last(), QString("& \"C:/Users/me/.local/bin/claude.exe\" update"));
    }
};

QTEST_GUILESS_MAIN(TestSetupWindows)
#include "tst_setup_windows.moc"
