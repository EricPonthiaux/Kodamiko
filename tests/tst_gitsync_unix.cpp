// SPDX-License-Identifier: GPL-3.0-only

#include "git/CredentialCache.h"
#include "git/FileChange.h"
#include "git/GitProcess.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest>

// git's credential cache, which keeps the credentials of each repository on
// the Unixes.
namespace {

void write(const QTemporaryDir &dir, const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(dir.filePath(path)).absolutePath());
    QFile file(dir.filePath(path));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QByteArray git(const QTemporaryDir &dir, const QStringList &arguments)
{
    return Git::run(dir.path(), arguments);
}

} // namespace

class TestGitSyncUnix : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("git unavailable");
        QVERIFY(config_.open());
        qputenv("GIT_CONFIG_GLOBAL", config_.fileName().toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        // The credential caches' sockets, away from the user's.
        QVERIFY(cache_.isValid());
        qputenv("XDG_CACHE_HOME", cache_.path().toUtf8());
    }

    void theHelperIsTheCacheOfTheRepository()
    {
        const QTemporaryDir project;

        const QString helper = CredentialCache::helper(project.path());

        QVERIFY(helper.startsWith("cache --timeout="));
        QVERIFY(helper.contains(CredentialCache::socketPath(project.path())));
    }

    void credentialsStayCachedPerRepository()
    {
        // Without askpass, a credential that is not cached fails instead of
        // prompting.
        qunsetenv("GIT_ASKPASS");
        qunsetenv("SSH_ASKPASS");
        QTemporaryDir project;
        QTemporaryDir other;
        git(project, {"init", "-q", "-b", "main"});
        git(other, {"init", "-q", "-b", "main"});
        write(project, "sub/a.txt", "a\n");
        const QString socket = CredentialCache::socketPath(project.path());
        QVERIFY(!socket.startsWith(project.path()));
        QCOMPARE(CredentialCache::socketPath(project.filePath("sub")), socket);
        QVERIFY(CredentialCache::socketPath(other.path()) != socket);

        Git::run(project.path(), {"credential", "approve"},
                 "protocol=https\nhost=example.com\nusername=u\npassword=s3cret\n\n");
        const QByteArray question = "protocol=https\nhost=example.com\n\n";
        const QByteArray filled = Git::run(project.filePath("sub"), {"credential", "fill"}, question);
        QVERIFY2(filled.contains("username=u\n") && filled.contains("password=s3cret\n"), filled.constData());
        QVERIFY_THROWS_EXCEPTION(GitError, Git::run(other.path(), {"credential", "fill"}, question));

        Git::run(project.path(), {"credential-cache", "exit", "--socket=" + socket});
    }

private:
    QTemporaryFile config_;
    QTemporaryDir cache_;
};

QTEST_GUILESS_MAIN(TestGitSyncUnix)
#include "tst_gitsync_unix.moc"
