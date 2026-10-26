// SPDX-License-Identifier: GPL-3.0-only

#include "git/CredentialCache.h"

#include <QTemporaryDir>
#include <QtTest>

// The credentials on Windows, where git's credential cache does not exist.
class TestGitSyncWindows : public QObject {
    Q_OBJECT

private slots:

    void leavesTheCredentialsToGitForWindows()
    {
        const QTemporaryDir project;

        // Git for Windows' credential manager keeps them.
        QVERIFY(CredentialCache::helper(project.path()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestGitSyncWindows)
#include "tst_gitsync_windows.moc"
