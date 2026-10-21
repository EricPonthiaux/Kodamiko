// SPDX-License-Identifier: GPL-3.0-only

#include "git/Askpass.h"
#include "git/Branches.h"
#include "git/CredentialCache.h"
#include "git/GitCommands.h"
#include "git/GitProcess.h"
#include "git/Integration.h"
#include "git/Remotes.h"
#include "git/RepositoryStatus.h"
#include "git/Snapshots.h"
#include "git/Stashes.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <thread>

// Exchanges with a remote (a bare repository on disk), branches, stashes,
// merges and rebases, and what supports them: progress, cancellation and
// credential prompts.

namespace {

void write(const QTemporaryDir &dir, const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(dir.filePath(path)).absolutePath());
    QFile file(dir.filePath(path));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QByteArray read(const QTemporaryDir &dir, const QString &path)
{
    QFile file(dir.filePath(path));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray git(const QTemporaryDir &dir, const QStringList &arguments)
{
    return Git::run(dir.path(), arguments);
}

void commit(const QTemporaryDir &dir, const QString &path, const QByteArray &content, const QString &message)
{
    write(dir, path, content);
    git(dir, {"add", "--", path});
    git(dir, {"commit", "-qm", message});
}

RepositoryInfo info(const QTemporaryDir &dir)
{
    const std::optional<RepositoryInfo> result = RepositoryInfo::read(dir.path());
    if (!result)
        qFatal("no git repository");
    return *result;
}

// A bare repository with one commit on main, and two clones of it.
struct Remote {
    QTemporaryDir bare;
    QTemporaryDir mine;
    QTemporaryDir theirs;

    Remote()
    {
        const QTemporaryDir seed;
        git(seed, {"init", "-q", "-b", "main"});
        commit(seed, "a.txt", "a\n", "init");
        git(bare, {"init", "-q", "--bare", "-b", "main"});
        git(seed, {"push", "-q", bare.path(), "main"});
        Git::run(QDir::tempPath(), {"clone", "-q", bare.path(), mine.path()});
        Git::run(QDir::tempPath(), {"clone", "-q", bare.path(), theirs.path()});
    }
};

} // namespace

class TestGitSync : public QObject {
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
        qputenv("GIT_AUTHOR_NAME", "t");
        qputenv("GIT_AUTHOR_EMAIL", "t@t");
        qputenv("GIT_COMMITTER_NAME", "t");
        qputenv("GIT_COMMITTER_EMAIL", "t@t");
    }

    void fetchCountsAheadAndBehind()
    {
        const Remote remote;
        QCOMPARE(info(remote.mine).upstream, QString("origin/main"));
        QCOMPARE(info(remote.mine).behind, 0);

        commit(remote.theirs, "b.txt", "b\n", "theirs");
        git(remote.theirs, {"push", "-q"});
        commit(remote.mine, "c.txt", "c\n", "mine");
        QCOMPARE(info(remote.mine).ahead, 1);

        QStringList progress;
        Git::Feedback feedback;
        feedback.progress = [&](const QString &line) { progress.append(line); };
        const QString message = Remotes::fetch(info(remote.mine), feedback);
        QVERIFY2(message.contains("1 new commit"), qPrintable(message));
        QCOMPARE(info(remote.mine).ahead, 1);
        QCOMPARE(info(remote.mine).behind, 1);
    }

    void pullFastForwardsOnly()
    {
        const Remote remote;
        commit(remote.theirs, "b.txt", "b\n", "theirs");
        git(remote.theirs, {"push", "-q"});
        Remotes::fetch(info(remote.mine), {});
        Remotes::pull(info(remote.mine), {});
        QCOMPARE(read(remote.mine, "b.txt"), QByteArray("b\n"));
        QCOMPARE(info(remote.mine).behind, 0);

        // Both sides moved on: no merge commit nobody asked for.
        commit(remote.theirs, "c.txt", "c\n", "theirs again");
        git(remote.theirs, {"push", "-q"});
        commit(remote.mine, "d.txt", "d\n", "mine");
        Remotes::fetch(info(remote.mine), {});
        try {
            Remotes::pull(info(remote.mine), {});
            QFAIL("the pull should have failed");
        } catch (const GitError &error) {
            QCOMPARE(error.kind(), GitError::Kind::Diverged);
        }
        QVERIFY(!QFile::exists(remote.mine.filePath("c.txt")));
    }

    void pullFollowsTheUsersChoice()
    {
        const Remote remote;
        commit(remote.theirs, "b.txt", "b\n", "theirs");
        git(remote.theirs, {"push", "-q"});
        commit(remote.mine, "c.txt", "c\n", "mine");
        git(remote.mine, {"config", "pull.rebase", "true"});
        Remotes::fetch(info(remote.mine), {});
        Remotes::pull(info(remote.mine), {});
        QCOMPARE(read(remote.mine, "b.txt"), QByteArray("b\n"));
        QCOMPARE(info(remote.mine).ahead, 1);
        QCOMPARE(info(remote.mine).behind, 0);
        // Rebased: no merge commit.
        QCOMPARE(git(remote.mine, {"rev-list", "--merges", "--count", "HEAD"}).trimmed(), QByteArray("0"));
    }

    void pushPublishesANewBranch()
    {
        const Remote remote;
        Branches::create(info(remote.mine), "feature", true);
        commit(remote.mine, "f.txt", "f\n", "feature");
        QVERIFY(info(remote.mine).upstream.isEmpty());

        const QString message = Remotes::push(info(remote.mine), {});
        QVERIFY2(message.contains("origin/feature"), qPrintable(message));
        QCOMPARE(info(remote.mine).upstream, QString("origin/feature"));
        QVERIFY(!git(remote.bare, {"branch", "--list", "feature"}).isEmpty());
    }

    void rejectedPushCanBeForced()
    {
        const Remote remote;
        commit(remote.theirs, "b.txt", "b\n", "theirs");
        git(remote.theirs, {"push", "-q"});
        commit(remote.mine, "c.txt", "c\n", "mine");
        Remotes::fetch(info(remote.mine), {});
        try {
            Remotes::push(info(remote.mine), {});
            QFAIL("the push should have been rejected");
        } catch (const GitError &error) {
            QCOMPARE(error.kind(), GitError::Kind::Rejected);
        }
        Remotes::push(info(remote.mine), {}, true);
        QCOMPARE(git(remote.bare, {"log", "-1", "--format=%s", "main"}).trimmed(), QByteArray("mine"));
    }

    void syncPullsThenPushes()
    {
        const Remote remote;
        git(remote.mine, {"config", "pull.rebase", "true"});
        commit(remote.theirs, "b.txt", "b\n", "theirs");
        git(remote.theirs, {"push", "-q"});
        commit(remote.mine, "c.txt", "c\n", "mine");
        Remotes::sync(info(remote.mine), {});
        QCOMPARE(info(remote.mine).ahead, 0);
        QCOMPARE(info(remote.mine).behind, 0);
        QCOMPARE(git(remote.bare, {"log", "-1", "--format=%s", "main"}).trimmed(), QByteArray("mine"));
    }

    void noRemote()
    {
        const QTemporaryDir project;
        git(project, {"init", "-q", "-b", "main"});
        commit(project, "a.txt", "a\n", "init");
        QVERIFY_THROWS_EXCEPTION(GitError, Remotes::fetch(info(project), {}));
        QVERIFY_THROWS_EXCEPTION(GitError, Remotes::push(info(project), {}));
        QCOMPARE(Remotes::defaultRemote({"upstream", "origin"}), QString("origin"));
        QCOMPARE(Remotes::defaultRemote({"upstream"}), QString("upstream"));
        QCOMPARE(Remotes::defaultRemote({"a", "b"}), QString());
    }

    void listBranches()
    {
        const Remote remote;
        git(remote.theirs, {"switch", "-qc", "topic"});
        commit(remote.theirs, "t.txt", "t\n", "topic work");
        git(remote.theirs, {"push", "-q", "-u", "origin", "topic"});
        Remotes::fetch(info(remote.mine), {});
        commit(remote.mine, "c.txt", "c\n", "mine");

        const QList<BranchInfo> branches = Branches::list(remote.mine.path());
        QCOMPARE(branches.size(), 3); // main, origin/main, origin/topic; not origin/HEAD
        QCOMPARE(branches[0].name, QString("main"));
        QVERIFY(branches[0].current && !branches[0].remote);
        QCOMPARE(branches[0].upstream, QString("origin/main"));
        QCOMPARE(branches[0].ahead, 1);
        QCOMPARE(branches[0].subject, QString("mine"));
        QCOMPARE(branches[2].name, QString("origin/topic"));
        QVERIFY(branches[2].remote && !branches[2].current);
        QCOMPARE(Branches::remoteOf("origin/feature/x"), QString("origin"));
        QCOMPARE(Branches::nameOnRemote("origin/feature/x"), QString("feature/x"));
    }

    void switchBranches()
    {
        const Remote remote;
        git(remote.theirs, {"switch", "-qc", "topic"});
        commit(remote.theirs, "t.txt", "t\n", "topic work");
        git(remote.theirs, {"push", "-q", "-u", "origin", "topic"});
        Remotes::fetch(info(remote.mine), {});

        // A remote branch: a local one is created to track it.
        Branches::switchTo(info(remote.mine), "origin/topic", true);
        QCOMPARE(info(remote.mine).branch, QString("topic"));
        QCOMPARE(info(remote.mine).upstream, QString("origin/topic"));
        QCOMPARE(read(remote.mine, "t.txt"), QByteArray("t\n"));

        // Changes that do not conflict come along, after a snapshot.
        write(remote.mine, "a.txt", "changed\n");
        const int snapshots = Snapshots::list(remote.mine.path()).size();
        Branches::switchTo(info(remote.mine), "main", false);
        QCOMPARE(info(remote.mine).branch, QString("main"));
        QCOMPARE(read(remote.mine, "a.txt"), QByteArray("changed\n"));
        QCOMPARE(Snapshots::list(remote.mine.path()).size(), snapshots + 1);

        // Back to the remote branch: its local branch exists already.
        Branches::switchTo(info(remote.mine), "origin/topic", true);
        QCOMPARE(info(remote.mine).branch, QString("topic"));
    }

    void switchOverConflictingChanges()
    {
        const QTemporaryDir project;
        git(project, {"init", "-q", "-b", "main"});
        commit(project, "a.txt", "a\n", "init");
        git(project, {"switch", "-qc", "other"});
        commit(project, "a.txt", "other\n", "other");
        git(project, {"switch", "-q", "main"});
        write(project, "a.txt", "local\n");

        try {
            Branches::switchTo(info(project), "other", false);
            QFAIL("the switch should have been refused");
        } catch (const GitError &error) {
            QCOMPARE(error.kind(), GitError::Kind::LocalChanges);
        }
        QCOMPARE(info(project).branch, QString("main"));

        // Stash and switch: the changes conflict with "other", so they stay
        // in the stash, and the conflict is left to resolve.
        QVERIFY_THROWS_EXCEPTION(GitError, Branches::stashAndSwitch(info(project), "other", false));
        QCOMPARE(info(project).branch, QString("other"));
        QCOMPARE(Stashes::list(project.path()).size(), 1);
    }

    void stashAndSwitchBringsChangesAlong()
    {
        const QTemporaryDir project;
        git(project, {"init", "-q", "-b", "main"});
        commit(project, "a.txt", "a\n", "init");
        commit(project, "b.txt", "b\n", "b");
        git(project, {"branch", "other", "HEAD~1"});
        write(project, "a.txt", "local\n");
        Branches::stashAndSwitch(info(project), "other", false);
        QCOMPARE(info(project).branch, QString("other"));
        QCOMPARE(read(project, "a.txt"), QByteArray("local\n"));
        QVERIFY(Stashes::list(project.path()).isEmpty());
    }

    void renameAndDeleteBranches()
    {
        const QTemporaryDir project;
        git(project, {"init", "-q", "-b", "main"});
        commit(project, "a.txt", "a\n", "init");
        git(project, {"switch", "-qc", "work"});
        commit(project, "w.txt", "w\n", "work");
        git(project, {"switch", "-q", "main"});

        Branches::rename(info(project), "work", "renamed");
        QCOMPARE(Branches::local(project.path()), QStringList({"main", "renamed"}));
        try {
            Branches::remove(info(project), "renamed", false);
            QFAIL("an unmerged branch should not be deleted");
        } catch (const GitError &error) {
            QCOMPARE(error.kind(), GitError::Kind::Unmerged);
        }
        Branches::remove(info(project), "renamed", true);
        QCOMPARE(Branches::local(project.path()), QStringList({"main"}));
    }

    void detachAtACommit()
    {
        const QTemporaryDir project;
        git(project, {"init", "-q", "-b", "main"});
        commit(project, "a.txt", "a\n", "init");
        const QString first = QString::fromUtf8(git(project, {"rev-parse", "--short", "HEAD"})).trimmed();
        commit(project, "a.txt", "b\n", "second");
        Branches::detach(info(project), first);
        QVERIFY(info(project).detached);
        QCOMPARE(read(project, "a.txt"), QByteArray("a\n"));
    }

    void removeRemoteBranchAndSetUpstream()
    {
        const Remote remote;
        git(remote.mine, {"push", "-q", "origin", "main:gone"});
        Remotes::fetch(info(remote.mine), {});
        Branches::create(info(remote.mine), "local", false);
        Branches::setUpstream(info(remote.mine), "local", "origin/gone");
        const QList<BranchInfo> branches = Branches::list(remote.mine.path());
        const auto local =
            std::find_if(branches.begin(), branches.end(), [](const BranchInfo &b) { return b.name == "local"; });
        QVERIFY(local != branches.end());
        QCOMPARE(local->upstream, QString("origin/gone"));
        Branches::removeRemote(info(remote.mine), "origin/gone", {});
        QVERIFY(git(remote.bare, {"branch", "--list", "gone"}).isEmpty());
    }

    void stashes()
    {
        const QTemporaryDir project;
        git(project, {"init", "-q", "-b", "main"});
        commit(project, "a.txt", "a\n", "init");
        QVERIFY_THROWS_EXCEPTION(GitError, Stashes::push(info(project), "nothing", true));

        write(project, "a.txt", "changed\n");
        write(project, "new.txt", "new\n");
        Stashes::push(info(project), "first", false);
        QCOMPARE(read(project, "a.txt"), QByteArray("a\n"));
        QVERIFY(QFile::exists(project.filePath("new.txt"))); // untracked: left in place
        Stashes::push(info(project), "second", true);
        QVERIFY(!QFile::exists(project.filePath("new.txt")));

        const QList<Stash> stashes = Stashes::list(project.path());
        QCOMPARE(stashes.size(), 2);
        QCOMPARE(stashes[0].index, 0);
        QVERIFY(stashes[0].message.endsWith("second"));
        QVERIFY(stashes[1].message.endsWith("first"));
        QVERIFY(Stashes::patch(project.path(), 1).contains("+changed"));
        QVERIFY(Stashes::patch(project.path(), 0).contains("+new"));

        Stashes::pop(info(project), 1);
        QCOMPARE(read(project, "a.txt"), QByteArray("changed\n"));
        Stashes::apply(info(project), 0);
        QCOMPARE(read(project, "new.txt"), QByteArray("new\n"));
        QCOMPARE(Stashes::list(project.path()).size(), 1);
        Stashes::drop(info(project), 0);
        QVERIFY(Stashes::list(project.path()).isEmpty());
    }

    void mergeConflictThenAbort()
    {
        const QTemporaryDir project;
        git(project, {"init", "-q", "-b", "main"});
        commit(project, "a.txt", "a\n", "init");
        git(project, {"switch", "-qc", "other"});
        commit(project, "a.txt", "other\n", "other");
        git(project, {"switch", "-q", "main"});
        commit(project, "a.txt", "main\n", "main");

        QVERIFY_THROWS_EXCEPTION(GitError, Integration::merge(info(project), "other"));
        QCOMPARE(info(project).operation, QString("merge"));
        QVERIFY(Integration::canContinue(info(project).operation));
        QVERIFY(!Integration::canSkip(info(project).operation));
        const std::optional<RepositoryStatus> status = RepositoryStatus::read(project.path());
        bool conflicted = false;
        for (const FileDiff &diff : status->unstaged + status->staged)
            conflicted |= diff.change.kind == ChangeKind::Conflicted && diff.change.path == "a.txt";
        QVERIFY(conflicted);

        Integration::abortOperation(info(project));
        QVERIFY(info(project).operation.isEmpty());
        QCOMPARE(read(project, "a.txt"), QByteArray("main\n"));
    }

    void mergeConflictThenContinue()
    {
        const QTemporaryDir project;
        git(project, {"init", "-q", "-b", "main"});
        commit(project, "a.txt", "a\n", "init");
        git(project, {"switch", "-qc", "other"});
        commit(project, "a.txt", "other\n", "other");
        git(project, {"switch", "-q", "main"});
        commit(project, "a.txt", "main\n", "main");

        QVERIFY_THROWS_EXCEPTION(GitError, Integration::merge(info(project), "other"));
        write(project, "a.txt", "resolved\n");
        git(project, {"add", "a.txt"});
        Integration::continueOperation(info(project));
        QVERIFY(info(project).operation.isEmpty());
        QCOMPARE(git(project, {"rev-list", "--merges", "--count", "HEAD"}).trimmed(), QByteArray("1"));
    }

    void mergeAndRebaseWithoutConflicts()
    {
        const QTemporaryDir project;
        git(project, {"init", "-q", "-b", "main"});
        commit(project, "a.txt", "a\n", "init");
        git(project, {"switch", "-qc", "other"});
        commit(project, "b.txt", "b\n", "other");
        git(project, {"switch", "-q", "main"});
        commit(project, "c.txt", "c\n", "main");

        Integration::rebase(info(project), "other");
        QCOMPARE(git(project, {"log", "--format=%s"}).trimmed(), QByteArray("main\nother\ninit"));

        git(project, {"switch", "-q", "other"});
        Integration::merge(info(project), "main");
        QCOMPARE(read(project, "c.txt"), QByteArray("c\n"));

        QVERIFY_THROWS_EXCEPTION(GitError, Integration::abortOperation(info(project))); // nothing in progress
    }

    void runReportsProgressAndCancels()
    {
        const QTemporaryDir project;
        git(project, {"init", "-q", "-b", "main"});
        std::atomic_bool cancel = false;
        Git::RunOptions options;
        options.feedback.cancel = &cancel;
        std::thread canceller([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            cancel = true;
        });
        QElapsedTimer timer;
        timer.start();
        try {
            Git::run(project.path(), {"-c", "alias.slow=!sleep 30", "slow"}, options);
            QFAIL("the command should have been cancelled");
        } catch (const GitError &error) {
            QCOMPARE(error.kind(), GitError::Kind::Cancelled);
        }
        canceller.join();
        QVERIFY(timer.elapsed() < 10000);

        QStringList lines;
        options = {};
        options.feedback.progress = [&](const QString &line) { lines.append(line); };
        Git::run(project.path(), {"-c", "alias.talk=!printf 'one 50%%\\rone 100%%\\ntwo\\n' >&2", "talk"}, options);
        QCOMPARE(lines, QStringList({"one 50%", "one 100%", "two"}));
    }

    void commandsReportProgressAndCancel()
    {
        const QTemporaryDir project;
        git(project, {"init", "-q", "-b", "main"});
        GitCommands commands;
        const QSignalSpy progress(&commands, &GitCommands::progress);
        const QSignalSpy failed(&commands, &GitCommands::failed);
        bool handled = false;
        commands.runLong(
            "Slow",
            [path = project.path()](const Git::Feedback &feedback) {
                Git::run(path, {"-c", "alias.slow=!echo started >&2; sleep 30", "slow"},
                         Git::RunOptions{{}, {}, feedback});
                return QString();
            },
            {},
            [&](const GitError &error) {
                handled = error.kind() == GitError::Kind::Cancelled;
                return true;
            });
        QTRY_VERIFY(progress.size() >= 2); // the start, then "started"
        QCOMPARE(progress.at(1).at(1).toString(), QString("started"));
        commands.cancel();
        QTRY_VERIFY(handled);
        QVERIFY(failed.isEmpty()); // handled
        QTRY_VERIFY(!commands.isBusy());
    }

    void askpassRelaysThePrompt()
    {
        QStringList prompts;
        AskpassServer server([&](const QString &prompt) -> std::optional<QString> {
            prompts.append(prompt);
            if (prompt.startsWith("Cancel"))
                return std::nullopt;
            return QStringLiteral("s3cret");
        });
        server.setHelperPath(DUET_ASKPASS_PATH);
        QVERIFY(server.listen());
        const QHash<QString, QString> environment = server.environment();
        QCOMPARE(environment.value("GIT_ASKPASS"), QString(DUET_ASKPASS_PATH));
        QCOMPARE(environment.value("SSH_ASKPASS_REQUIRE"), QString("force"));

        const auto ask = [&](const QString &prompt, int &exitCode) {
            QProcess helper;
            QProcessEnvironment processEnvironment = QProcessEnvironment::systemEnvironment();
            for (auto it = environment.begin(); it != environment.end(); ++it)
                processEnvironment.insert(it.key(), it.value());
            helper.setProcessEnvironment(processEnvironment);
            helper.start(DUET_ASKPASS_PATH, {prompt});
            // The server answers from the event loop.
            QElapsedTimer waited;
            waited.start();
            while (helper.state() != QProcess::NotRunning && waited.elapsed() < 10000)
                QTest::qWait(20);
            exitCode = helper.exitCode();
            return helper.readAllStandardOutput();
        };
        int exitCode = -1;
        QCOMPARE(ask("Password for 'https://u@example.com': ", exitCode), QByteArray("s3cret\n"));
        QCOMPARE(exitCode, 0);
        QCOMPARE(ask("Cancel me", exitCode), QByteArray());
        QCOMPARE(exitCode, 1);
        QCOMPARE(prompts, QStringList({"Password for 'https://u@example.com': ", "Cancel me"}));

        // End to end: git itself asks through duet-askpass.
        prompts.clear();
        QTemporaryDir project;
        git(project, {"init", "-q", "-b", "main"});
        QByteArray filled;
        std::thread asking([&] {
            Git::RunOptions options;
            options.input = "protocol=https\nhost=example.com\n\n";
            options.feedback.environment = environment;
            filled = Git::run(project.path(), {"credential", "fill"}, options);
        });
        QTRY_VERIFY_WITH_TIMEOUT(prompts.size() == 2, 10000); // user name, then password
        asking.join();
        QVERIFY2(filled.contains("username=s3cret\n") && filled.contains("password=s3cret\n"), filled.constData());
        QVERIFY(prompts.at(0).startsWith("Username for 'https://example.com'"));

        QVERIFY(AskpassServer::isConfirmation("Are you sure you want to continue connecting (yes/no/[fingerprint])? "));
        QVERIFY(!AskpassServer::isConfirmation("Enter passphrase for key '/home/u/.ssh/id_ed25519': "));
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

QTEST_GUILESS_MAIN(TestGitSync)
#include "tst_gitsync.moc"
