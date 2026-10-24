// SPDX-License-Identifier: GPL-3.0-only

#include "system/Executables.h"
#include "system/FilePaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QtTest>

// What every system's implementation of src/system does;
// tst_system_windows.cpp and tst_system_unix.cpp check what differs.
class TestSystem : public QObject {
    Q_OBJECT

private slots:

    void everySystemAcceptsAnOrdinaryName()
    {
        QVERIFY(FilePaths::systemAcceptsName("notes.txt"));
        QVERIFY(FilePaths::systemAcceptsName("my notes"));
    }

    void aFilePathGoesThroughAUrlUnchanged()
    {
        const QString path = QDir::rootPath() + "project/site/index.html";

        const QString urlPath = FilePaths::toUrlPath(path);

        QVERIFY(urlPath.startsWith('/'));
        QCOMPARE(FilePaths::fromUrlPath(urlPath), path);
    }

    void aUrlPathIsCleaned()
    {
        const QString urlPath = FilePaths::toUrlPath(QDir::rootPath() + "project/site/../data.txt");

        QCOMPARE(FilePaths::fromUrlPath(urlPath), QDir::rootPath() + "project/data.txt");
    }

    void findsAProgramBesideTheApplication()
    {
        const QString name = QFileInfo(QCoreApplication::applicationFilePath()).completeBaseName();

        QCOMPARE(QFileInfo(Executables::besideTheApplication(name)),
                 QFileInfo(QCoreApplication::applicationFilePath()));
    }

    void findsNoMissingProgramBesideTheApplication()
    {
        QVERIFY(Executables::besideTheApplication("no-such-program-of-duet").isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSystem)
#include "tst_system.moc"
