// SPDX-License-Identifier: GPL-3.0-only

#include "setup/Prerequisites.h"

#include <QtTest>

// The installs on the Unixes: shell scripts.
class TestSetupUnix : public QObject {
    Q_OBJECT

private slots:

    void installRunsInTheSystemShell()
    {
        Prerequisite claude;
        claude.installCommand = "curl -fsSL https://claude.ai/install.sh | bash";

        const ProcessSpec spec = Prerequisites::installProcess(claude, "/tmp");

        QCOMPARE(spec.program, QString("/bin/sh"));
        QCOMPARE(spec.arguments, (QStringList{"-c", claude.installCommand}));
    }

    void updateCallsTheProgramFound()
    {
        Prerequisite claude;
        claude.installCommand = "claude update";
        claude.path = "/home/me/.local/bin/claude";

        const ProcessSpec spec = Prerequisites::installProcess(claude, "/tmp");

        QCOMPARE(spec.arguments.last(), QString("\"/home/me/.local/bin/claude\" update"));
    }
};

QTEST_GUILESS_MAIN(TestSetupUnix)
#include "tst_setup_unix.moc"
