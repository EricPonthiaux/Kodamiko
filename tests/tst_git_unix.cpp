// SPDX-License-Identifier: GPL-3.0-only

#include "git/FileChange.h"
#include "git/GitOperations.h"
#include "git/GitProcess.h"
#include "git/RepositoryStatus.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest>

// The repository's hooks, shell scripts that git runs.
namespace {

void write(const QTemporaryDir &dir, const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(dir.filePath(path)).absolutePath());
    QFile file(dir.filePath(path));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

void initRepository(const QTemporaryDir &dir, const QHash<QString, QByteArray> &files)
{
    for (auto it = files.begin(); it != files.end(); ++it)
        write(dir, it.key(), it.value());
    Git::run(dir.path(), {"init", "-q", "-b", "main"});
    Git::run(dir.path(), {"add", "-A"});
    Git::run(dir.path(), {"commit", "-qm", "init"});
}

} // namespace

class TestGitUnix : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("git unavailable");
        // Neither the user's configuration (commit signing…) nor the
        // system's.
        QVERIFY(config_.open());
        qputenv("GIT_CONFIG_GLOBAL", config_.fileName().toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_AUTHOR_NAME", "t");
        qputenv("GIT_AUTHOR_EMAIL", "t@t");
        qputenv("GIT_COMMITTER_NAME", "t");
        qputenv("GIT_COMMITTER_EMAIL", "t@t");
    }

    void commitRunsTheRepositoryHooks()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        write(project, ".git/hooks/pre-commit", "#!/bin/sh\necho 'rejected by the hook' >&2\nexit 1\n");
        QFile::setPermissions(project.filePath(".git/hooks/pre-commit"),
                              QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        write(project, "a.txt", "b\n");
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        GitOperations::stage(info, {"a.txt"});
        try {
            GitOperations::commit(info, "message", false);
            QFAIL("the hook should have rejected the commit");
        } catch (const GitError &error) {
            QVERIFY(QString::fromStdString(error.what()).contains("rejected by the hook"));
        }
    }

private:
    QTemporaryFile config_;
};

QTEST_GUILESS_MAIN(TestGitUnix)
#include "tst_git_unix.moc"
