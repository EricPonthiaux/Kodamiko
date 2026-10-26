// SPDX-License-Identifier: GPL-3.0-only

#include "system/Executables.h"
#include "system/FilePaths.h"
#include "system/Host.h"

#include <QtTest>

// The Unixes' implementation of src/system.
class TestSystemUnix : public QObject {
    Q_OBJECT

private slots:

    void theHostIsAUnix() { QVERIFY(Host::operatingSystem() != OperatingSystem::Windows); }

    void anExecutableKeepsItsName() { QCOMPARE(Executables::fileName("bin/clangd"), QString("bin/clangd")); }

    void aVirtualEnvironmentKeepsPythonInBin()
    {
        QCOMPARE(Executables::virtualEnvironmentPython("/p/.venv"), QString("/p/.venv/bin/python"));
    }

    void pythonIsCalledPython3First() { QCOMPARE(Executables::pythonNames().first(), QString("python3")); }

    void acceptsWhatWindowsRefuses()
    {
        QVERIFY(FilePaths::systemAcceptsName("a:b"));
        QVERIFY(FilePaths::systemAcceptsName("name."));
    }

    void aUrlPathIsTheFilePath()
    {
        QCOMPARE(FilePaths::toUrlPath("/site/index.html"), QString("/site/index.html"));
        QCOMPARE(FilePaths::fromUrlPath("/site/index.html"), QString("/site/index.html"));
    }

    void aFileUrlGivesItsPathUnchanged()
    {
        QCOMPARE(FilePaths::fromFileUrl(QUrl("file:///home/me/Project/main.ts")), QString("/home/me/Project/main.ts"));
    }
};

QTEST_GUILESS_MAIN(TestSystemUnix)
#include "tst_system_unix.moc"
