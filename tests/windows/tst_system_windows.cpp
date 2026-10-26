// SPDX-License-Identifier: GPL-3.0-only

#include "system/Executables.h"
#include "system/FilePaths.h"
#include "system/Host.h"

#include <QtTest>

// Windows' implementation of src/system.
class TestSystemWindows : public QObject {
    Q_OBJECT

private slots:

    void theHostIsWindows() { QCOMPARE(Host::operatingSystem(), OperatingSystem::Windows); }

    void anExecutableEndsWithExe() { QCOMPARE(Executables::fileName("bin/clangd"), QString("bin/clangd.exe")); }

    void aVirtualEnvironmentKeepsPythonInScripts()
    {
        QCOMPARE(Executables::virtualEnvironmentPython("C:/p/.venv"), QString("C:/p/.venv/Scripts/python.exe"));
    }

    void pythonIsCalledPythonFirst() { QCOMPARE(Executables::pythonNames().first(), QString("python")); }

    void refusesTheReservedCharacters()
    {
        for (const QString &name : {"a:b", "a<b", "a>b", "a\"b", "a|b", "a?b", "a*b", "a\\b"})
            QVERIFY2(!FilePaths::systemAcceptsName(name), qPrintable(name));
    }

    void refusesAFinalDotOrSpace()
    {
        QVERIFY(!FilePaths::systemAcceptsName("name."));
        QVERIFY(!FilePaths::systemAcceptsName("name "));
    }

    void aDriveTakesASlashInAUrl()
    {
        QCOMPARE(FilePaths::toUrlPath("C:/site/index.html"), QString("/C:/site/index.html"));
        QCOMPARE(FilePaths::fromUrlPath("/C:/site/index.html"), QString("C:/site/index.html"));
    }

    void aLanguageServersLowercaseDriveComesBackInUppercase()
    {
        QCOMPARE(FilePaths::fromFileUrl(QUrl("file:///c%3A/project/main.ts")), QString("C:/project/main.ts"));
        QCOMPARE(FilePaths::fromFileUrl(QUrl("file:///D:/project/main.ts")), QString("D:/project/main.ts"));
    }
};

QTEST_GUILESS_MAIN(TestSystemWindows)
#include "tst_system_windows.moc"
