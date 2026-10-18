#include "setup/Prerequisites.h"

#include <QtTest>

class TestSetup : public QObject {
    Q_OBJECT

private slots:

    void parsesVersionOutputs()
    {
        QCOMPARE(Prerequisites::parseVersion("git version 2.47.1"), QVersionNumber(2, 47, 1));
        QCOMPARE(Prerequisites::parseVersion("2.1.123 (Claude Code)"), QVersionNumber(2, 1, 123));
        QCOMPARE(Prerequisites::parseVersion("git version 2.39.5 (Apple Git-154)"), QVersionNumber(2, 39, 5));
        QVERIFY(Prerequisites::parseVersion("no version here").isNull());
    }

    void choosesThePackageManager()
    {
        QCOMPARE(Prerequisites::linuxInstallCommand("ubuntu", "debian", "git"), QString("sudo apt install -y git"));
        QCOMPARE(Prerequisites::linuxInstallCommand("cachyos", "arch", "git"), QString("sudo pacman -S --needed git"));
        QCOMPARE(Prerequisites::linuxInstallCommand("rocky", "rhel centos fedora", "git"),
                 QString("sudo dnf install -y git"));
        QCOMPARE(Prerequisites::linuxInstallCommand("opensuse-tumbleweed", "opensuse suse", "git"),
                 QString("sudo zypper install -y git"));
        QVERIFY(Prerequisites::linuxInstallCommand("nixos", "", "git").isEmpty());
    }

    void installRunsInAShell()
    {
        Prerequisite claude;
        claude.installCommand = "curl -fsSL https://claude.ai/install.sh | bash";
        const ProcessSpec spec = Prerequisites::installProcess(claude, "/tmp");
#ifndef Q_OS_WIN
        QCOMPARE(spec.program, QString("/bin/sh"));
        QCOMPARE(spec.arguments, (QStringList{"-c", claude.installCommand}));
#endif
        QCOMPARE(spec.workingDirectory, QString("/tmp"));
    }

    void findsProgramsOnThisMachine()
    {
        const QList<Prerequisite> prerequisites = Prerequisites::check();
        QCOMPARE(prerequisites.size(), 2);
        QCOMPARE(prerequisites[0].id, Prerequisite::Id::Git);
        QVERIFY(!prerequisites[0].path.isEmpty()); // the tests need git anyway
        QCOMPARE(prerequisites[0].status, Prerequisite::Status::Ok);
    }
};

QTEST_GUILESS_MAIN(TestSetup)
#include "tst_setup.moc"
