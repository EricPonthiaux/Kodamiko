// SPDX-License-Identifier: GPL-3.0-only

#include "agent/AgentMergeQueue.h"
#include "git/Branches.h"
#include "git/ConflictMarkers.h"
#include "git/FilePatch.h"
#include "git/FileStatuses.h"
#include "git/GitCommands.h"
#include "git/GitOperations.h"
#include "git/GitProcess.h"
#include "git/LineChanges.h"
#include "git/RepositoryStatus.h"
#include "git/Snapshots.h"
#include "git/Worktrees.h"
#include "support/SystemFixtures.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>
#include <ranges>

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

// Contents of a file in the index.
QByteArray indexed(const QTemporaryDir &dir, const QString &path)
{
    return git(dir, {"show", ":" + path});
}

// Repository with a first commit containing `files`.
void initRepository(const QTemporaryDir &dir, const QHash<QString, QByteArray> &files)
{
    for (auto it = files.begin(); it != files.end(); ++it)
        write(dir, it.key(), it.value());
    git(dir, {"init", "-q", "-b", "main"});
    // libgit2 reads the system's configuration despite GIT_CONFIG_NOSYSTEM:
    // on Windows, its core.autocrlf would give the index's text in CRLF.
    git(dir, {"config", "core.autocrlf", "false"});
    if (!files.isEmpty()) {
        git(dir, {"add", "-A"});
        git(dir, {"commit", "-qm", "init"});
    }
}

RepositoryStatus status(const QTemporaryDir &dir)
{
    const std::optional<RepositoryStatus> result = RepositoryStatus::read(dir.path());
    if (!result)
        qFatal("no git repository");
    return *result;
}

const FileDiff *find(const QList<FileDiff> &diffs, const QString &path)
{
    for (const FileDiff &diff : diffs) {
        if (diff.change.path == path)
            return &diff;
    }
    return nullptr;
}

// "+" / "-" lines of the patch whose text satisfies `accept`.
PatchSelection selectLines(const FilePatch &patch, const std::function<bool(const QByteArray &)> &accept)
{
    PatchSelection selection;
    for (int i = 0; i < patch.hunks.size(); ++i) {
        for (int j = 0; j < patch.hunks[i].lines.size(); ++j) {
            if (patch.hunks[i].isChange(j) && accept(patch.hunks[i].lines[j]))
                selection[i].insert(j);
        }
    }
    return selection;
}

} // namespace

class TestGit : public QObject {
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

    void statusOutsideARepository()
    {
        const QTemporaryDir project;
        write(project, "a.txt", "a\n");
        QVERIFY(!RepositoryStatus::read(project.path()));
        QVERIFY(!RepositoryInfo::read(project.path()));
    }

    void statusSeparatesStagedAndUnstaged()
    {
        const QTemporaryDir project;
        initRepository(project, {{"staged.cpp", "int a;\n"}, {"both.txt", "1\n"}, {"gone.txt", "bye\n"}});
        write(project, "staged.cpp", "int b;\n");
        write(project, "both.txt", "2\n");
        git(project, {"add", "staged.cpp", "both.txt"});
        write(project, "both.txt", "3\n");
        QFile::remove(project.filePath("gone.txt"));
        write(project, "src/new.js", "let x;\n");

        const RepositoryStatus result = status(project);
        QCOMPARE(result.info.branch, QString("main"));
        QVERIFY(!result.info.unborn && !result.info.detached);
        QCOMPARE(result.info.headMessage, QString("init\n"));

        QCOMPARE(result.staged.size(), 2);
        QCOMPARE(find(result.staged, "staged.cpp")->change.kind, ChangeKind::Modified);
        QVERIFY(find(result.staged, "both.txt")->patch.contains("+2"));

        QCOMPARE(result.unstaged.size(), 3);
        QVERIFY(find(result.unstaged, "both.txt")->patch.contains("-2\n+3"));
        QCOMPARE(find(result.unstaged, "gone.txt")->change.kind, ChangeKind::Deleted);
        QCOMPARE(find(result.unstaged, "src/new.js")->change.kind, ChangeKind::Added);
        QVERIFY(find(result.unstaged, "src/new.js")->patch.contains("+let x;"));
    }

    void statusKeepsTheNewVersionOfModifiedFiles()
    {
        const QTemporaryDir project;
        initRepository(project, {{"both.txt", "1\n"}, {"gone.txt", "bye\n"}});
        write(project, "both.txt", "2\n");
        git(project, {"add", "both.txt"});
        write(project, "both.txt", "3\n");
        QFile::remove(project.filePath("gone.txt"));
        write(project, "new.txt", "hi\n");

        const RepositoryStatus result = status(project);

        QCOMPARE(find(result.staged, "both.txt")->newText, QByteArray("2\n"));   // the index's
        QCOMPARE(find(result.unstaged, "both.txt")->newText, QByteArray("3\n")); // the disk's
        // Their patch shows them whole.
        QVERIFY(find(result.unstaged, "gone.txt")->newText.isEmpty());
        QVERIFY(find(result.unstaged, "new.txt")->newText.isEmpty());
    }

    void statusCanIgnoreWhitespace()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a b\nc\n"}});
        write(project, "a.txt", "a   b\nd\n");

        const auto ignoring = RepositoryStatus::read(project.path(), Whitespace::Ignore);
        const auto comparing = RepositoryStatus::read(project.path(), Whitespace::Compare);

        QVERIFY(ignoring && comparing);
        const QByteArray ignored = find(ignoring->unstaged, "a.txt")->patch;
        QVERIFY(ignored.contains("-c\n+d\n"));
        QVERIFY(!ignored.contains("-a b"));
        QVERIFY(find(comparing->unstaged, "a.txt")->patch.contains("-a b\n"));
    }

    void statusListsFilesOverOneMebibyteWithoutTheirDiff()
    {
        const QTemporaryDir project;
        initRepository(project, {{"data.csv", "a\n"}});
        const QByteArray large(qsizetype(2) * 1024 * 1024, 'x');
        write(project, "data.csv", large);
        write(project, "dump.log", large);
        write(project, "small.txt", "s\n");

        const RepositoryStatus result = status(project);

        QCOMPARE(find(result.unstaged, "dump.log")->change.kind, ChangeKind::Added);
        QVERIFY(find(result.unstaged, "dump.log")->isTooLarge);
        QVERIFY(find(result.unstaged, "dump.log")->patch.isEmpty());
        QCOMPARE(find(result.unstaged, "data.csv")->change.kind, ChangeKind::Modified);
        QVERIFY(find(result.unstaged, "data.csv")->isTooLarge);
        QVERIFY(!find(result.unstaged, "small.txt")->isTooLarge);
        QVERIFY(find(result.unstaged, "small.txt")->patch.contains("+s"));
    }

    void statusTellsWhatAConflictedFileHolds()
    {
        const QTemporaryDir dir;
        initRepository(dir, {{"text.txt", "a\n"}, {"logo.bin", QByteArray("\x89PNG\0base", 9)}, {"gone.txt", "a\n"}});
        git(dir, {"switch", "-qc", "other"});
        write(dir, "text.txt", "other\n");
        write(dir, "logo.bin", QByteArray("\x89PNG\0other", 10));
        git(dir, {"rm", "-q", "gone.txt"});
        git(dir, {"commit", "-qam", "other"});
        git(dir, {"switch", "-q", "main"});
        write(dir, "text.txt", "main\n");
        write(dir, "logo.bin", QByteArray("\x89PNG\0main", 9));
        write(dir, "gone.txt", "main\n");
        git(dir, {"commit", "-qam", "main"});
        QVERIFY_THROWS_EXCEPTION(GitError, git(dir, {"merge", "other"}));

        const RepositoryStatus result = status(dir);

        const auto conflictOf = [&result](const QString &path) {
            const FileDiff *diff = find(result.unstaged, path);
            return diff ? diff->conflict : ConflictContent::None;
        };
        QCOMPARE(conflictOf("text.txt"), ConflictContent::Markers);
        QCOMPARE(conflictOf("logo.bin"), ConflictContent::Binary);
        QCOMPARE(conflictOf("gone.txt"), ConflictContent::NoMarkers);
    }

    void statusOfASubfolderUsesRelativePaths()
    {
        const QTemporaryDir project;
        initRepository(project, {{"root.txt", "r\n"}, {"sub/file.txt", "f\n"}});
        write(project, "root.txt", "r2\n");
        write(project, "sub/file.txt", "f2\n");

        const std::optional<RepositoryStatus> result = RepositoryStatus::read(project.filePath("sub"));
        QVERIFY(result);
        QCOMPARE(result->info.prefix, QString("sub/"));
        QCOMPARE(result->info.repoPath("file.txt"), QString("sub/file.txt"));
        QCOMPARE(result->unstaged.size(), 1);
        QCOMPARE(result->unstaged.first().change, (FileChange{ChangeKind::Modified, "file.txt", {}}));
    }

    void statusOfARepositoryWithoutCommit()
    {
        const QTemporaryDir project;
        initRepository(project, {});
        write(project, "a.txt", "a\n");
        git(project, {"add", "a.txt"});

        const RepositoryStatus result = status(project);
        QVERIFY(result.info.unborn);
        QCOMPARE(result.info.branch, QString("main"));
        QVERIFY(result.info.headId.isEmpty());
        QCOMPARE(result.staged.size(), 1);
        QCOMPARE(result.staged.first().change.kind, ChangeKind::Added);
    }

    void statusOfADetachedHead()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        git(project, {"checkout", "-q", "--detach"});
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        QVERIFY(info.detached);
        QVERIFY(info.branch.isEmpty());
        QCOMPARE(info.headId, QString::fromUtf8(git(project, {"rev-parse", "--short", "HEAD"})).trimmed());
    }

    void filePatchParsesAndLocatesLines()
    {
        const QByteArray text = "diff --git a/f b/f\n"
                                "index 1..2 100644\n"
                                "--- a/f\n"
                                "+++ b/f\n"
                                "@@ -1,2 +1,2 @@ heading\n"
                                " a\n"
                                "-b\n"
                                "+B\n"
                                "@@ -10 +10 @@\n"
                                "-x\n"
                                "+X\n";
        const FilePatch patch = FilePatch::parse(text);
        QCOMPARE(patch.header.size(), 4);
        QCOMPARE(patch.hunks.size(), 2);
        QCOMPARE(patch.hunks[0].heading, QByteArray(" heading"));
        QCOMPARE(patch.hunks[1].oldCount, 1);
        QCOMPARE(patch.lineCount(), static_cast<int>(text.count('\n')));
        QCOMPARE(patch.locate(0), std::make_pair(-1, -1));
        QCOMPARE(patch.locate(4), std::make_pair(0, -1));
        QCOMPARE(patch.locate(6), std::make_pair(0, 1));
        QCOMPARE(patch.locate(9), std::make_pair(1, 0));
        QVERIFY(patch.partial({}, PatchDirection::Forward).isEmpty());
        // The second hunk alone: its context line does not belong to the first.
        QCOMPARE(patch.partial(patch.selectHunk(1), PatchDirection::Forward),
                 QByteArray("diff --git a/f b/f\nindex 1..2 100644\n--- a/f\n+++ b/f\n@@ -10,1 +10,1 @@\n-x\n+X\n"));
    }

    void stageSelectedLines()
    {
        const QTemporaryDir project;
        initRepository(project, {{"f.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n"}});
        write(project, "f.txt", "1\nTWO\n3\n4\n5\n6\n7\n8\n9\n10\nELEVEN\n12\nthirteen\n");

        RepositoryStatus result = status(project);
        const FilePatch patch = FilePatch::parse(result.unstaged.first().patch);
        QCOMPARE(patch.hunks.size(), 2);
        // Only the lines of the second hunk that touch "11", not the final addition.
        const PatchSelection selection =
            selectLines(patch, [](const QByteArray &line) { return line == "-11" || line == "+ELEVEN"; });
        GitOperations::applyToIndex(result.info, patch.partial(selection, PatchDirection::Forward),
                                    PatchDirection::Forward);
        QCOMPARE(indexed(project, "f.txt"), QByteArray("1\n2\n3\n4\n5\n6\n7\n8\n9\n10\nELEVEN\n12\n"));

        // Then the first hunk: its start does not depend on what is already staged.
        result = status(project);
        const FilePatch rest = FilePatch::parse(result.unstaged.first().patch);
        GitOperations::applyToIndex(result.info, rest.partial(rest.selectHunk(0), PatchDirection::Forward),
                                    PatchDirection::Forward);
        QCOMPARE(indexed(project, "f.txt"), QByteArray("1\nTWO\n3\n4\n5\n6\n7\n8\n9\n10\nELEVEN\n12\n"));
        QCOMPARE(read(project, "f.txt"), QByteArray("1\nTWO\n3\n4\n5\n6\n7\n8\n9\n10\nELEVEN\n12\nthirteen\n"));
    }

    void unstageSelectedLines()
    {
        const QTemporaryDir project;
        initRepository(project, {{"f.txt", "a\nb\nc\n"}});
        write(project, "f.txt", "A\nb\nC\nd\n");
        git(project, {"add", "f.txt"});

        const RepositoryStatus result = status(project);
        const FilePatch patch = FilePatch::parse(result.staged.first().patch);
        const PatchSelection selection =
            selectLines(patch, [](const QByteArray &line) { return line == "-c" || line == "+C"; });
        GitOperations::applyToIndex(result.info, patch.partial(selection, PatchDirection::Reverse),
                                    PatchDirection::Reverse);
        QCOMPARE(indexed(project, "f.txt"), QByteArray("A\nb\nc\nd\n"));
        QCOMPARE(read(project, "f.txt"), QByteArray("A\nb\nC\nd\n"));
    }

    void partialChangesOfCreatedAndDeletedFiles()
    {
        const QTemporaryDir project;
        initRepository(project, {{"old.txt", "x\ny\nz\n"}});
        write(project, "new.txt", "1\n2\n3\n");
        QFile::remove(project.filePath("old.txt"));

        // Part of an untracked file: it is created in the index.
        RepositoryStatus result = status(project);
        FilePatch created = FilePatch::parse(find(result.unstaged, "new.txt")->patch);
        GitOperations::applyToIndex(
            result.info,
            created.partial(selectLines(created, [](const QByteArray &line) { return line != "+2"; }),
                            PatchDirection::Forward),
            PatchDirection::Forward);
        QCOMPARE(indexed(project, "new.txt"), QByteArray("1\n3\n"));

        // Part of a deletion: the file stays in the index.
        const FilePatch deleted = FilePatch::parse(find(result.unstaged, "old.txt")->patch);
        GitOperations::applyToIndex(
            result.info,
            deleted.partial(selectLines(deleted, [](const QByteArray &line) { return line == "-y"; }),
                            PatchDirection::Forward),
            PatchDirection::Forward);
        QCOMPARE(indexed(project, "old.txt"), QByteArray("x\nz\n"));

        // Unstaging part of a created file: it stays in the index.
        result = status(project);
        created = FilePatch::parse(find(result.staged, "new.txt")->patch);
        GitOperations::applyToIndex(
            result.info,
            created.partial(selectLines(created, [](const QByteArray &line) { return line == "+3"; }),
                            PatchDirection::Reverse),
            PatchDirection::Reverse);
        QCOMPARE(indexed(project, "new.txt"), QByteArray("1\n"));
    }

    void stageLinesWithoutFinalNewlineAndCrlf()
    {
        const QTemporaryDir project;
        initRepository(project, {{"crlf.txt", "a\r\nb\r\nc\r\n"}, {"eof.txt", "a\nb"}});
        write(project, "crlf.txt", "A\r\nb\r\nC\r\n");
        write(project, "eof.txt", "a\nB");

        const RepositoryStatus result = status(project);
        const FilePatch crlf = FilePatch::parse(find(result.unstaged, "crlf.txt")->patch);
        GitOperations::applyToIndex(result.info,
                                    crlf.partial(selectLines(crlf,
                                                             [](const QByteArray &line) {
                                                                 return line.startsWith("-c") || line.startsWith("+C");
                                                             }),
                                                 PatchDirection::Forward),
                                    PatchDirection::Forward);
        QCOMPARE(indexed(project, "crlf.txt"), QByteArray("a\r\nb\r\nC\r\n"));

        const FilePatch eof = FilePatch::parse(find(result.unstaged, "eof.txt")->patch);
        GitOperations::applyToIndex(result.info, eof.partial(eof.selectHunk(0), PatchDirection::Forward),
                                    PatchDirection::Forward);
        QCOMPARE(indexed(project, "eof.txt"), QByteArray("a\nB"));
    }

    void stageAndUnstageFiles()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}, {"b.txt", "b\n"}});
        write(project, "a.txt", "A\n");
        QFile::remove(project.filePath("b.txt"));
        write(project, "c.txt", "c\n");

        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        GitOperations::stage(info, {"a.txt", "b.txt", "c.txt"});
        RepositoryStatus result = status(project);
        QCOMPARE(result.staged.size(), 3);
        QVERIFY(result.unstaged.isEmpty());

        GitOperations::unstage(result.info, {"b.txt", "c.txt"});
        result = status(project);
        QCOMPARE(result.staged.size(), 1);
        QCOMPARE(result.unstaged.size(), 2);
    }

    void unstageInARepositoryWithoutCommit()
    {
        const QTemporaryDir project;
        initRepository(project, {});
        write(project, "a.txt", "a\n");
        git(project, {"add", "a.txt"});
        GitOperations::unstage(*RepositoryInfo::read(project.path()), {"a.txt"});
        const RepositoryStatus result = status(project);
        QVERIFY(result.staged.isEmpty());
        QCOMPARE(result.unstaged.size(), 1);
    }

    void commitAndAmend()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        write(project, "a.txt", "b\n");
        RepositoryInfo info = *RepositoryInfo::read(project.path());
        GitOperations::stage(info, {"a.txt"});

        const QString id = GitOperations::commit(info, "#12 first — café ✓\n\ndetails", false);
        info = *RepositoryInfo::read(project.path());
        QCOMPARE(info.headId, id);
        QCOMPARE(info.headMessage, QString("#12 first — café ✓\n\ndetails\n"));
        QVERIFY(status(project).staged.isEmpty());

        GitOperations::commit(info, "reworded", true);
        QCOMPARE(QString::fromUtf8(git(project, {"rev-list", "--count", "HEAD"})).trimmed(), QString("2"));
        QCOMPARE(RepositoryInfo::read(project.path())->headMessage, QString("reworded\n"));
    }

    void commitWithoutAnIdentityAsksForOneThenSucceedsOnceItIsSet()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        // No identity from the environment nor guessed from the machine.
        git(project, {"config", "user.useConfigOnly", "true"});
        const QList<QByteArray> identityVariables = {"GIT_AUTHOR_NAME", "GIT_AUTHOR_EMAIL", "GIT_COMMITTER_NAME",
                                                     "GIT_COMMITTER_EMAIL"};
        for (const QByteArray &variable : identityVariables)
            qunsetenv(variable.constData());
        const auto restoreIdentity = qScopeGuard([&identityVariables] {
            for (const QByteArray &variable : identityVariables)
                qputenv(variable.constData(), variable.endsWith("EMAIL") ? "t@t" : "t");
        });
        write(project, "a.txt", "b\n");
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        GitOperations::stage(info, {"a.txt"});

        try {
            GitOperations::commit(info, "message", false);
            QFAIL("git should not know who commits");
        } catch (const GitError &error) {
            QCOMPARE(error.kind(), GitError::Kind::NoIdentity);
        }
        GitOperations::setIdentity(info, {.name = "Ada", .email = "ada@example.com"},
                                   GitOperations::ConfigScope::Repository);
        GitOperations::commit(info, "message", false);

        const GitOperations::Identity identity = GitOperations::identity(info);
        QCOMPARE(identity.name, QString("Ada"));
        QCOMPARE(identity.email, QString("ada@example.com"));
        QCOMPARE(QString::fromUtf8(git(project, {"log", "-1", "--format=%an <%ae>"})).trimmed(),
                 QString("Ada <ada@example.com>"));
    }

    void rollbackRestoresTheLastCommitStagedChangesIncluded()
    {
        const QTemporaryDir project;
        initRepository(project, {{"staged.txt", "a\n"}, {"both.txt", "b\n"}});
        write(project, "staged.txt", "staged\n");
        write(project, "both.txt", "staged\n");
        write(project, "new.txt", "new\n");
        git(project, {"add", "-A"});
        write(project, "both.txt", "staged then modified\n");

        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        GitOperations::rollback(info, {"staged.txt", "both.txt", "new.txt"});

        QCOMPARE(read(project, "staged.txt"), QByteArray("a\n"));
        QCOMPARE(read(project, "both.txt"), QByteArray("b\n"));
        QVERIFY(!QFileInfo::exists(project.filePath("new.txt")));
        QVERIFY(git(project, {"status", "--porcelain"}).isEmpty());
        QCOMPARE(Snapshots::list(project.path()).size(), 1); // the changes can come back
    }

    void rollbackWithoutACommitRemovesTheNewFiles()
    {
        const QTemporaryDir project;
        initRepository(project, {});
        write(project, "new.txt", "new\n");
        git(project, {"add", "new.txt"});

        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        GitOperations::rollback(info, {"new.txt"});

        QVERIFY(!QFileInfo::exists(project.filePath("new.txt")));
        QVERIFY(git(project, {"ls-files"}).isEmpty());
    }

    void discardKeepsASnapshot()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}, {"b.txt", "b\n"}});
        write(project, "a.txt", "modified\n");
        write(project, "b.txt", "staged\n");
        git(project, {"add", "b.txt"});
        write(project, "b.txt", "staged then modified\n");
        write(project, "dir/untracked.txt", "untracked\n");

        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        GitOperations::discard(info, {"a.txt", "b.txt", "dir/untracked.txt"});
        QCOMPARE(read(project, "a.txt"), QByteArray("a\n"));
        QCOMPARE(read(project, "b.txt"), QByteArray("staged\n")); // index version
        QVERIFY(!QFileInfo::exists(project.filePath("dir")));
        QCOMPARE(indexed(project, "b.txt"), QByteArray("staged\n"));

        QList<Snapshot> snapshots = Snapshots::list(project.path());
        QCOMPARE(snapshots.size(), 1);
        QCOMPARE(snapshots.first().paths, QStringList({"a.txt", "b.txt", "dir/untracked.txt"}));
        // Outside the branches: the history has not moved.
        QCOMPARE(QString::fromUtf8(git(project, {"rev-list", "--count", "HEAD"})).trimmed(), QString("1"));

        Snapshots::restore(info, snapshots.first());
        QCOMPARE(read(project, "a.txt"), QByteArray("modified\n"));
        QCOMPARE(read(project, "b.txt"), QByteArray("staged then modified\n"));
        QCOMPARE(read(project, "dir/untracked.txt"), QByteArray("untracked\n"));
        QCOMPARE(indexed(project, "b.txt"), QByteArray("staged\n"));
        // The restore itself is reversible.
        snapshots = Snapshots::list(project.path());
        QCOMPARE(snapshots.size(), 2);
        QVERIFY(snapshots.first().reason.startsWith("Before restoring"));
    }

    void discardSelectedLines()
    {
        const QTemporaryDir project;
        initRepository(project, {{"f.txt", "a\nb\nc\n"}});
        write(project, "f.txt", "A\nb\nC\n");

        const RepositoryStatus result = status(project);
        const FilePatch patch = FilePatch::parse(result.unstaged.first().patch);
        const PatchSelection selection =
            selectLines(patch, [](const QByteArray &line) { return line == "-a" || line == "+A"; });
        GitOperations::discardPatch(result.info, {"f.txt"}, patch.partial(selection, PatchDirection::Reverse));
        QCOMPARE(read(project, "f.txt"), QByteArray("a\nb\nC\n"));
        QCOMPARE(Snapshots::list(project.path()).size(), 1);
    }

    void snapshotsArePruned()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        for (int i = 0; i < Snapshots::maximumCount + 2; ++i)
            Snapshots::create(info, QString::number(i), {"a.txt"});
        const QList<Snapshot> snapshots = Snapshots::list(project.path());
        QCOMPARE(snapshots.size(), Snapshots::maximumCount);
        QCOMPARE(snapshots.first().reason, QString::number(Snapshots::maximumCount + 1));
    }

    void snapshotsOfEnlumineAreAdopted()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        const QString ref = Snapshots::create(info, "old", {"a.txt"});
        const QString legacy = QString(ref).replace("refs/duet/", "refs/enlumine/");
        git(project, {"update-ref", legacy, ref});
        git(project, {"update-ref", "-d", ref});

        const QList<Snapshot> snapshots = Snapshots::list(project.path());
        QCOMPARE(snapshots.size(), 1);
        QCOMPARE(snapshots.first().ref, ref);
        QCOMPARE(snapshots.first().reason, QString("old"));
        QVERIFY(git(project, {"for-each-ref", "refs/enlumine/"}).isEmpty());
    }

    void snapshotOfWorkingTreeSkipsUnchangedStates()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}, {"b.txt", "b\n"}});
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        // A clean working directory is recorded too: it is what a prompt
        // starting from it would be undone to.
        QVERIFY(!Snapshots::createOfWorkingTree(info, "clean").isEmpty());
        QVERIFY(Snapshots::createOfWorkingTree(info, "still clean").isEmpty());

        write(project, "a.txt", "modified\n");
        write(project, "new.txt", "untracked\n");
        QVERIFY(!Snapshots::createOfWorkingTree(info, "first").isEmpty());
        QVERIFY(Snapshots::createOfWorkingTree(info, "same state").isEmpty());
        write(project, "a.txt", "modified again\n");
        QVERIFY(!Snapshots::createOfWorkingTree(info, "second").isEmpty());

        const QList<Snapshot> snapshots = Snapshots::list(project.path());
        QCOMPARE(snapshots.size(), 3);
        QCOMPARE(snapshots.first().reason, QString("second"));
        QCOMPARE(snapshots.first().paths, QStringList({"a.txt", "new.txt"}));
        QVERIFY(snapshots.first().wholeWorkingTree);
        QVERIFY(snapshots.last().paths.isEmpty());

        write(project, "a.txt", "lost\n");
        QCOMPARE(Snapshots::restore(info, snapshots.first()), 1);
        QCOMPARE(read(project, "a.txt"), QByteArray("modified again\n"));
    }

    void snapshotOfWorkingTreeUndoesAPrompt()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}, {"b.txt", "b\n"}, {".gitignore", "*.log\n"}});
        write(project, "a.txt", "pending\n");
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        QVERIFY(!Snapshots::createOfWorkingTree(info, "before the prompt").isEmpty());

        // What Claude does during the prompt: files clean until then are
        // changed, created or deleted.
        write(project, "a.txt", "pending, then edited\n");
        write(project, "b.txt", "edited\n");
        write(project, "dir/created.txt", "created\n");
        write(project, "build.log", "ignored\n");
        write(project, "b.txt", "rewritten\n");
        git(project, {"add", "b.txt"});

        const Snapshot before = Snapshots::list(project.path()).first();
        QCOMPARE(Snapshots::restore(info, before), 3);
        QCOMPARE(read(project, "a.txt"), QByteArray("pending\n"));
        QCOMPARE(read(project, "b.txt"), QByteArray("b\n"));
        QVERIFY(!QFileInfo::exists(project.filePath("dir")));
        QCOMPARE(read(project, "build.log"), QByteArray("ignored\n"));
        QCOMPARE(indexed(project, "b.txt"), QByteArray("rewritten\n")); // the index is not touched

        // The restore can itself be undone.
        const Snapshot restored = Snapshots::list(project.path()).first();
        QVERIFY(restored.reason.startsWith("Before restoring"));
        QVERIFY(restored.wholeWorkingTree);
        Snapshots::restore(info, restored);
        QCOMPARE(read(project, "a.txt"), QByteArray("pending, then edited\n"));
        QCOMPARE(read(project, "b.txt"), QByteArray("rewritten\n"));
        QCOMPARE(read(project, "dir/created.txt"), QByteArray("created\n"));
    }

    void snapshotOfWorkingTreeInUnbornRepository()
    {
        const QTemporaryDir project;
        initRepository(project, {});
        write(project, "a.txt", "a\n");
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        QVERIFY(!Snapshots::createOfWorkingTree(info, "before").isEmpty());
        write(project, "a.txt", "edited\n");
        write(project, "b.txt", "created\n");
        Snapshots::restore(info, Snapshots::list(project.path()).first());
        QCOMPARE(read(project, "a.txt"), QByteArray("a\n"));
        QVERIFY(!QFileInfo::exists(project.filePath("b.txt")));
    }

    void commandsRunInOrderAndReportErrors()
    {
        GitCommands commands;
        QSignalSpy succeeded(&commands, &GitCommands::succeeded);
        QSignalSpy failed(&commands, &GitCommands::failed);
        const QSignalSpy busy(&commands, &GitCommands::busyChanged);
        QStringList order;
        bool callbackRan = false;
        commands.run(
            "one",
            [&order] {
                order << "one";
                return QString("done");
            },
            [&callbackRan] { callbackRan = true; });
        commands.run("two", []() -> QString { throw GitError("failure"); });
        QVERIFY(commands.isBusy());
        QTRY_COMPARE(busy.size(), 2);
        QVERIFY(!commands.isBusy());
        QVERIFY(callbackRan);
        QCOMPARE(succeeded.size(), 1);
        QCOMPARE(succeeded.first().at(1).toString(), QString("done"));
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.first().at(1).toString(), QString("failure"));
    }

    void fileStatusesForTheTree()
    {
        const QTemporaryDir project;
        initRepository(project,
                       {{"src/kept.cpp", "k\n"}, {"src/changed.cpp", "c\n"}, {".gitignore", "build/\n*.log\n"}});
        write(project, "src/changed.cpp", "c2\n");
        write(project, "src/new/added.cpp", "a\n");
        git(project, {"add", "src/new/added.cpp"});
        write(project, "notes.txt", "n\n");
        write(project, "build/out.o", "o");
        write(project, "debug.log", "l");

        const QString root = QDir::cleanPath(project.path());
        const FileStatusMap statuses = FileStatuses::read(root);
        QCOMPARE(statuses.value(root + "/src/changed.cpp"), FileStatus::Modified);
        QCOMPARE(statuses.value(root + "/src/new/added.cpp"), FileStatus::Added);
        QCOMPARE(statuses.value(root + "/notes.txt"), FileStatus::Untracked);
        QCOMPARE(statuses.value(root + "/build"), FileStatus::Ignored); // the folder, not its files
        QCOMPARE(statuses.value(root + "/debug.log"), FileStatus::Ignored);
        QVERIFY(!statuses.contains(root + "/src/kept.cpp"));
        QVERIFY(!statuses.contains(root + "/build/out.o"));

        // Folders take the worst state of their files.
        const FileStatusMap folders = FileStatuses::folderStatuses(statuses, root);
        QCOMPARE(folders.value(root + "/src"), FileStatus::Modified);
        QCOMPARE(folders.value(root + "/src/new"), FileStatus::Added);
        QCOMPARE(folders.value(root), FileStatus::Modified);
        QVERIFY(!folders.contains(root + "/build"));

        // From a subfolder, paths stay absolute and only its files appear.
        const FileStatusMap sub = FileStatuses::read(root + "/src");
        QCOMPARE(sub.value(root + "/src/changed.cpp"), FileStatus::Modified);
        QVERIFY(!sub.contains(root + "/notes.txt"));

        const QTemporaryDir outside;
        QVERIFY(FileStatuses::read(outside.path()).isEmpty());
    }

    void fileStatusReaderSignalsChanges()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        FileStatusReader reader;
        const QSignalSpy changed(&reader, &FileStatusReader::statusesChanged);
        reader.setFolder(project.path());
        write(project, "a.txt", "b\n");
        reader.refresh(); // merged with the read in progress
        const QString file = QDir::cleanPath(project.path()) + "/a.txt";
        QTRY_VERIFY(reader.statuses().contains(file));
        QCOMPARE(reader.statuses().value(file), FileStatus::Modified);
        QVERIFY(changed.size() >= 2); // emptied, then read
    }

    void lineChangesOfEachKind()
    {
        const QByteArray base = "a\nb\nc\nd\ne\n";
        // b modified, a line added after d, e deleted.
        const QList<LineChange> changes = LineChanges::compute(base, "a\nB\nc\nd\nnew\n");
        QCOMPARE(changes.size(), 2);
        QCOMPARE(changes[0].kind(), LineChange::Kind::Modified);
        QCOMPARE(changes[0].start, 1);
        QCOMPARE(changes[0].count, 1);
        QCOMPARE(changes[0].baseStart, 1);
        QCOMPARE(changes[1].kind(), LineChange::Kind::Modified); // e replaced by new

        const QList<LineChange> added = LineChanges::compute(base, "a\nb\nx\ny\nc\nd\ne\n");
        QCOMPARE(added.size(), 1);
        QCOMPARE(added[0].kind(), LineChange::Kind::Added);
        QCOMPARE(added[0].start, 2);
        QCOMPARE(added[0].count, 2);
        QCOMPARE(added[0].baseStart, 2);
        QCOMPARE(added[0].baseCount, 0);

        const QList<LineChange> deleted = LineChanges::compute(base, "a\nd\ne\n");
        QCOMPARE(deleted.size(), 1);
        QCOMPARE(deleted[0].kind(), LineChange::Kind::Deleted);
        QCOMPARE(deleted[0].start, 1); // the marker goes on "d"
        QCOMPARE(deleted[0].baseStart, 1);
        QCOMPARE(deleted[0].baseCount, 2);
        QVERIFY(deleted[0].covers(1, 3));
        QVERIFY(!deleted[0].covers(0, 3));

        QVERIFY(LineChanges::compute(base, base).isEmpty());
    }

    void lineChangesRevertEveryKind()
    {
        const QByteArray base = "a\nb\nc\nd\ne";
        const QList<QByteArray> currents = {"a\nB\nc\nd\ne",    "a\nb\nx\nc\nd\ne", "a\nd\ne", "a\nb\nc\nd\ne\n",
                                            "z\na\nb\nc\nd\ne", "a\nb\nc\nd",       ""};
        for (const QByteArray &current : currents) {
            QByteArray text = current;
            // Undoing the changes one by one, from the last, gives the base back.
            const QList<LineChange> changes = LineChanges::compute(base, text);
            QVERIFY2(!changes.isEmpty(), current.constData());
            for (const auto &change : std::views::reverse(changes))
                text = LineChanges::revert(base, text, change);
            QCOMPARE(text, base);
        }
    }

    void lineChangesBaseIsTheIndex()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "one\n"}});
        write(project, "a.txt", "two\n");
        QCOMPARE(*LineChanges::indexVersion(project.filePath("a.txt")), QByteArray("one\n"));
        git(project, {"add", "a.txt"});
        QCOMPARE(*LineChanges::indexVersion(project.filePath("a.txt")), QByteArray("two\n"));

        write(project, "untracked.txt", "x\n");
        QVERIFY(!LineChanges::indexVersion(project.filePath("untracked.txt")));
        const QTemporaryDir outside;
        write(outside, "b.txt", "x\n");
        QVERIFY(!LineChanges::indexVersion(outside.filePath("b.txt")));
    }

    void branchNamesFollowGitsRules()
    {
        for (const char *valid : {"main", "feature/login", "fix-42", "release/v1.2", "user@work"})
            QVERIFY2(Branches::nameError(valid).isEmpty(), valid);
        for (const char *invalid :
             {"",       "with space", "a..b", "-x",  "a/", "a.", "/a",  "a//b", ".hidden", "a/.b",
              "x.lock", "a~1",        "a^",   "a:b", "a?", "a*", "a[b", "a\\b", "@",       "a@{b"})
            QVERIFY2(!Branches::nameError(invalid).isEmpty(), invalid);
        QCOMPARE(Branches::sanitizedName("Fix the  login bug"), QString("Fix-the-login-bug"));
        QCOMPARE(Branches::sanitizedName("a..b~c"), QString("a.bc"));
    }

    void createBranch()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        write(project, "a.txt", "uncommitted\n");
        const RepositoryInfo info = *RepositoryInfo::read(project.path());

        Branches::create(info, "feature/one", false);
        QCOMPARE(Branches::local(project.path()), QStringList({"feature/one", "main"}));
        QCOMPARE(RepositoryInfo::read(project.path())->branch, QString("main"));

        Branches::create(info, "feature/two", true);
        QCOMPARE(RepositoryInfo::read(project.path())->branch, QString("feature/two"));
        QCOMPARE(read(project, "a.txt"), QByteArray("uncommitted\n")); // the changes come along

        QVERIFY_THROWS_EXCEPTION(GitError, Branches::create(info, "feature/one", false)); // already exists
    }

    void createBranchWithoutCommits()
    {
        const QTemporaryDir project;
        git(project, {"init", "-q", "-b", "main"});
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        QVERIFY(info.unborn);
        Branches::create(info, "start", true);
        QCOMPARE(RepositoryInfo::read(project.path())->branch, QString("start"));
    }

    void createsAWorktreeOnItsOwnBranchHiddenFromTheCheckout()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const RepositoryInfo info = *RepositoryInfo::read(project.path());

        const Worktree worktree = Worktrees::add(info, "agent-1");

        QCOMPARE(worktree.branch, QString("duet/agent-1"));
        // The repository's paths have their symbolic links resolved (/private/var on macOS).
        QCOMPARE(worktree.path, QDir(info.workDir).filePath(".claude/worktrees/agent-1"));
        QCOMPARE(read(project, ".claude/worktrees/agent-1/a.txt"), QByteArray("a\n"));
        QVERIFY(Branches::local(project.path()).contains("duet/agent-1"));
        QVERIFY(git(project, {"status", "--porcelain"}).isEmpty());
    }

    void excludesTheWorktreesFolderOnlyOnce()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const RepositoryInfo info = *RepositoryInfo::read(project.path());

        Worktrees::add(info, "agent-1");
        Worktrees::add(info, "agent-2");

        QCOMPARE(read(project, ".git/info/exclude").count("/.claude/worktrees/"), 1);
        QVERIFY(read(project, ".gitignore").isEmpty());
    }

    void suggestsTheFirstUnusedAgentName()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        QCOMPARE(Worktrees::unusedName(info), QString("agent-1"));

        Worktrees::add(info, "agent-1");
        git(project, {"branch", "duet/agent-2"});

        QCOMPARE(Worktrees::unusedName(info), QString("agent-3"));
    }

    void aWorktreeHoldsWorkOnceChangedOrCommitted()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const Worktree worktree = Worktrees::add(*RepositoryInfo::read(project.path()), "agent-1");
        QVERIFY(!Worktrees::hasWork(worktree));

        write(project, ".claude/worktrees/agent-1/a.txt", "changed\n");
        QVERIFY(Worktrees::hasWork(worktree));

        Git::run(worktree.path, {"commit", "-qam", "change"});
        QVERIFY(Worktrees::hasWork(worktree));
    }

    void removesTheWorktreeAndItsBranch()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        const Worktree worktree = Worktrees::add(info, "agent-1");
        write(project, ".claude/worktrees/agent-1/new.txt", "unsaved work\n");

        Worktrees::remove(info, worktree);

        QVERIFY(!QFileInfo::exists(worktree.path));
        QVERIFY(!Branches::local(project.path()).contains("duet/agent-1"));
    }

    void removesALockedWorktreeAndItsBranch()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        const Worktree worktree = Worktrees::add(info, "agent-1");
        git(project, {"worktree", "lock", worktree.path});

        Worktrees::remove(info, worktree);

        QVERIFY(!QFileInfo::exists(worktree.path));
        QVERIFY(!Branches::local(project.path()).contains("duet/agent-1"));
    }

    void removesTheBranchOfAWorktreeWhoseFolderIsGone()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        const Worktree worktree = Worktrees::add(info, "agent-1");
        QVERIFY(QDir(worktree.path).removeRecursively());

        Worktrees::remove(info, worktree);

        QVERIFY(!Branches::local(project.path()).contains("duet/agent-1"));
    }

    void findsTheWorktreeThatHasABranchCheckedOut()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        const Worktree worktree = Worktrees::add(info, "agent-1");

        const std::optional<QString> folder = Worktrees::folderHolding(info, "duet/agent-1");

        QVERIFY(folder);
        QCOMPARE(QFileInfo(*folder).canonicalFilePath(), QFileInfo(worktree.path).canonicalFilePath());
        QVERIFY(!Worktrees::folderHolding(info, info.branch)); // the checkout's own
    }

    void refusesToDeleteABranchAWorktreeHasCheckedOut()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        Worktrees::add(info, "agent-1");

        try {
            Branches::remove(info, "duet/agent-1", false);
            QFAIL("the branch was deleted");
        } catch (const GitError &error) {
            QCOMPARE(error.kind(), GitError::Kind::InWorktree);
            QVERIFY(error.message().contains(".claude/worktrees/agent-1"));
        }
    }

    void refusesAWorktreeNameGitCannotUse()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        QVERIFY_THROWS_EXCEPTION(GitError, Worktrees::add(*RepositoryInfo::read(project.path()), "two words"));
    }

    void commitsAWorktreesChangesOnlyWhenItHasSome()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const Worktree worktree = Worktrees::add(*RepositoryInfo::read(project.path()), "agent-1");

        Worktrees::commitChanges(worktree, "nothing");
        QVERIFY(!Worktrees::hasCommitsToMerge(worktree));

        write(project, ".claude/worktrees/agent-1/new.txt", "new\n");
        Worktrees::commitChanges(worktree, "agent-1: add new.txt");

        QVERIFY(Worktrees::hasCommitsToMerge(worktree));
        QCOMPARE(Git::run(worktree.path, {"log", "-1", "--format=%s"}).trimmed(), QByteArray("agent-1: add new.txt"));
    }

    void undoesAConflictingRebaseAndNamesTheFiles()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const Worktree worktree = Worktrees::add(*RepositoryInfo::read(project.path()), "agent-1");
        write(project, ".claude/worktrees/agent-1/a.txt", "agent\n");
        Git::run(worktree.path, {"commit", "-qam", "agent"});
        write(project, "a.txt", "user\n");
        git(project, {"commit", "-qam", "user"});

        const RebaseResult result = Worktrees::rebase(worktree, "main");

        QCOMPARE(result.conflicts, QStringList{"a.txt"});
        QCOMPARE(read(project, ".claude/worktrees/agent-1/a.txt"), QByteArray("agent\n"));
        QVERIFY(!QFileInfo::exists(Git::run(worktree.path, {"rev-parse", "--git-path", "rebase-merge"}).trimmed()));
    }

    void leavesTheRebaseAnAgentIsInTheMiddleOfAlone()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const Worktree worktree = Worktrees::add(*RepositoryInfo::read(project.path()), "agent-1");
        write(project, ".claude/worktrees/agent-1/a.txt", "agent\n");
        Git::run(worktree.path, {"commit", "-qam", "agent"});
        write(project, "a.txt", "user\n");
        git(project, {"commit", "-qam", "user"});
        QVERIFY_THROWS_EXCEPTION(GitError, Git::run(worktree.path, {"rebase", "main"}));

        const RebaseResult result = Worktrees::rebase(worktree, "main");

        QCOMPARE(result.pendingOperation, QString("rebase"));
        QVERIFY(!result.succeeded());
        QVERIFY(QFileInfo::exists(Git::run(worktree.path, {"rev-parse", "--git-path", "rebase-merge"}).trimmed()));
    }

    void mergesAnAgentsCommitsIntoTheCurrentBranch()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const Worktree worktree = Worktrees::add(*RepositoryInfo::read(project.path()), "agent-1");
        write(project, ".claude/worktrees/agent-1/agent.txt", "agent\n");
        Worktrees::commitChanges(worktree, "agent work");
        write(project, "user.txt", "user\n");
        git(project, {"add", "user.txt"});
        git(project, {"commit", "-qm", "user work"});
        GitCommands commands;
        AgentMergeQueue queue(&commands);
        queue.setFolder(project.path());
        QSignalSpy merged(&queue, &AgentMergeQueue::merged);

        queue.enqueue(worktree);

        QVERIFY(merged.wait());
        QCOMPARE(read(project, "agent.txt"), QByteArray("agent\n"));
        QCOMPARE(git(project, {"log", "--format=%s"}), QByteArray("agent work\nuser work\ninit\n"));
        QCOMPARE(merged.first().at(1).toString(), QString("main"));
    }

    void sendsConflictingCommitsBackToTheAgent()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const Worktree worktree = Worktrees::add(*RepositoryInfo::read(project.path()), "agent-1");
        write(project, ".claude/worktrees/agent-1/a.txt", "agent\n");
        Worktrees::commitChanges(worktree, "agent work");
        write(project, "a.txt", "user\n");
        git(project, {"commit", "-qam", "user work"});
        GitCommands commands;
        AgentMergeQueue queue(&commands);
        queue.setFolder(project.path());
        QSignalSpy sentBack(&queue, &AgentMergeQueue::sentBack);

        queue.enqueue(worktree);

        QVERIFY(sentBack.wait());
        QVERIFY(sentBack.first().at(1).toString().contains("a.txt"));
        QCOMPARE(sentBack.first().at(2).toString(), Worktrees::head(worktree));
        QCOMPARE(read(project, "a.txt"), QByteArray("user\n"));
    }

    void sendsCommitsThatFailTheCheckBackWithItsOutput()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const Worktree worktree = Worktrees::add(*RepositoryInfo::read(project.path()), "agent-1");
        write(project, ".claude/worktrees/agent-1/agent.txt", "agent\n");
        Worktrees::commitChanges(worktree, "agent work");
        GitCommands commands;
        AgentMergeQueue queue(&commands);
        queue.setFolder(project.path());
        queue.setCheckCommand(SystemFixtures::printingThenExiting("tests broken", 3));
        QSignalSpy sentBack(&queue, &AgentMergeQueue::sentBack);

        queue.enqueue(worktree);

        QVERIFY(sentBack.wait());
        const QString report = sentBack.first().at(1).toString();
        QVERIFY(report.contains("tests broken"));
        QVERIFY(report.contains("exit code 3"));
        QVERIFY(!QFileInfo::exists(project.filePath("agent.txt")));
    }

    void mergesCommitsThatPassTheCheck()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const Worktree worktree = Worktrees::add(*RepositoryInfo::read(project.path()), "agent-1");
        write(project, ".claude/worktrees/agent-1/agent.txt", "agent\n");
        Worktrees::commitChanges(worktree, "agent work");
        GitCommands commands;
        AgentMergeQueue queue(&commands);
        queue.setFolder(project.path());
        queue.setCheckCommand(SystemFixtures::succeedingIfFileExists("agent.txt"));
        QSignalSpy merged(&queue, &AgentMergeQueue::merged);

        queue.enqueue(worktree);

        QVERIFY(merged.wait());
        QCOMPARE(read(project, "agent.txt"), QByteArray("agent\n"));
    }

    void leavesTheUsersChangesAloneWhenTheyAreInTheWay()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const Worktree worktree = Worktrees::add(*RepositoryInfo::read(project.path()), "agent-1");
        write(project, ".claude/worktrees/agent-1/a.txt", "agent\n");
        Worktrees::commitChanges(worktree, "agent work");
        write(project, "a.txt", "unsaved user change\n");
        GitCommands commands;
        AgentMergeQueue queue(&commands);
        queue.setFolder(project.path());
        QSignalSpy waiting(&queue, &AgentMergeQueue::waiting);

        queue.enqueue(worktree);

        QVERIFY(waiting.wait());
        QCOMPARE(read(project, "a.txt"), QByteArray("unsaved user change\n"));
        QCOMPARE(waiting.first().at(1).toStringList(), QStringList{"a.txt"});
    }

    void mergesWaitingWorkOnceTheUsersFilesAreClean()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const Worktree worktree = Worktrees::add(*RepositoryInfo::read(project.path()), "agent-1");
        write(project, ".claude/worktrees/agent-1/a.txt", "agent\n");
        Worktrees::commitChanges(worktree, "agent work");
        write(project, "a.txt", "user change\n");
        GitCommands commands;
        AgentMergeQueue queue(&commands);
        queue.setFolder(project.path());
        FileStatusReader statuses;
        statuses.setFolder(project.path());
        queue.watch(&statuses);
        QSignalSpy waiting(&queue, &AgentMergeQueue::waiting);
        QSignalSpy merged(&queue, &AgentMergeQueue::merged);
        QSignalSpy statusesChanged(&statuses, &FileStatusReader::statusesChanged);
        queue.enqueue(worktree);
        QVERIFY(waiting.wait());

        // The file states change while a.txt is still changed: it waits on.
        write(project, "notes.txt", "user notes\n");
        statuses.refresh();
        QVERIFY(statusesChanged.wait());
        QCOMPARE(queue.waitingWorktrees().size(), 1);
        QCOMPARE(merged.size(), 0);

        // a.txt discarded: the next file states bring the work in.
        git(project, {"checkout", "--", "a.txt"});
        statuses.refresh();

        QVERIFY(merged.wait());
        QCOMPARE(read(project, "a.txt"), QByteArray("agent\n"));
        QCOMPARE(read(project, "notes.txt"), QByteArray("user notes\n"));
        QVERIFY(queue.waitingWorktrees().isEmpty());
    }

    void dropsTheWaitingWorkOfADeletedWorktree()
    {
        const QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const RepositoryInfo repository = *RepositoryInfo::read(project.path());
        const Worktree worktree = Worktrees::add(repository, "agent-1");
        write(project, ".claude/worktrees/agent-1/a.txt", "agent\n");
        Worktrees::commitChanges(worktree, "agent work");
        write(project, "a.txt", "user change\n");
        GitCommands commands;
        AgentMergeQueue queue(&commands);
        queue.setFolder(project.path());
        FileStatusReader statuses;
        statuses.setFolder(project.path());
        queue.watch(&statuses);
        QSignalSpy waiting(&queue, &AgentMergeQueue::waiting);
        QSignalSpy statusesChanged(&statuses, &FileStatusReader::statusesChanged);
        queue.enqueue(worktree);
        QVERIFY(waiting.wait());
        Worktrees::remove(repository, worktree);
        QSignalSpy progress(&queue, &AgentMergeQueue::progress);

        git(project, {"checkout", "--", "a.txt"});
        statuses.refresh();

        QVERIFY(statusesChanged.wait());
        QVERIFY(queue.waitingWorktrees().isEmpty());
        QCOMPARE(progress.size(), 0); // no merge started
    }

    void forgetsTheWaitingWorkOfAnotherFolder()
    {
        const QTemporaryDir project;
        const QTemporaryDir other;
        initRepository(project, {{"a.txt", "a\n"}});
        const Worktree worktree = Worktrees::add(*RepositoryInfo::read(project.path()), "agent-1");
        write(project, ".claude/worktrees/agent-1/a.txt", "agent\n");
        Worktrees::commitChanges(worktree, "agent work");
        write(project, "a.txt", "user change\n");
        GitCommands commands;
        AgentMergeQueue queue(&commands);
        queue.setFolder(project.path());
        QSignalSpy waiting(&queue, &AgentMergeQueue::waiting);
        queue.enqueue(worktree);
        QVERIFY(waiting.wait());

        queue.setFolder(other.path());

        QVERIFY(queue.waitingWorktrees().isEmpty());
    }

    void namesTheFilesThatKeepAMergeOut()
    {
        RepositoryInfo repository;
        repository.workDir = "/repo";
        const QString error = "error: Your local changes to the following files would be overwritten by merge:\n"
                              "\ta.txt\n\tsrc/b.cpp\n"
                              "Please commit your changes or stash them before you merge.\nAborting";

        QCOMPARE(Worktrees::blockingFiles(repository, error), QStringList({"/repo/a.txt", "/repo/src/b.cpp"}));
    }

    void findsTheSidesOfAConflict()
    {
        const QByteArray text = "a\n<<<<<<< HEAD\nmine\n=======\ntheirs\nmore\n>>>>>>> other\nz\n";

        const QList<ConflictMarkers::Conflict> conflicts = ConflictMarkers::find(text);

        QCOMPARE(conflicts.size(), 1);
        const ConflictMarkers::Conflict &conflict = conflicts.first();
        QCOMPARE(conflict.startLine, 1);
        QCOMPARE(conflict.separatorLine, 3);
        QCOMPARE(conflict.endLine, 6);
        QVERIFY(!conflict.baseLine);
        QCOMPARE(conflict.currentLabel, QString("HEAD"));
        QCOMPARE(conflict.incomingLabel, QString("other"));
        const auto bytes = [&text](ConflictMarkers::Section section) {
            return text.sliced(section.start, section.end - section.start);
        };
        QCOMPARE(bytes(conflict.current), QByteArray("mine\n"));
        QCOMPARE(bytes(conflict.incoming), QByteArray("theirs\nmore\n"));
        QCOMPARE(bytes(conflict.span), text.sliced(2, text.size() - 4));
    }

    void findsTheBaseOfADiff3ConflictWithWindowsLineEndings()
    {
        const QByteArray text = "<<<<<<< HEAD\r\nmine\r\n||||||| base\r\nold\r\n=======\r\ntheirs\r\n>>>>>>> other\r\n";

        const QList<ConflictMarkers::Conflict> conflicts = ConflictMarkers::find(text);

        QCOMPARE(conflicts.size(), 1);
        QCOMPARE(conflicts.first().baseLine, std::optional(2));
        QCOMPARE(conflicts.first().incomingLabel, QString("other"));
        const ConflictMarkers::Section current = conflicts.first().current;
        QCOMPARE(text.sliced(current.start, current.end - current.start), QByteArray("mine\r\n"));
    }

    void ignoresMarkersOutOfSequence()
    {
        // A Markdown heading's underline, an unclosed conflict, an end
        // marker alone, a longer run of the marker's character.
        const QByteArray text = "Title\n=======\n>>>>>>> x\n<<<<<<<< no\n<<<<<<< HEAD\nmine\n=======\n";

        QVERIFY(ConflictMarkers::find(text).isEmpty());
    }

    void startsAgainAtAStartMarkerInsideAConflict()
    {
        const QByteArray text = "<<<<<<< a\nx\n<<<<<<< b\ny\n=======\nz\n>>>>>>> c\n";

        const QList<ConflictMarkers::Conflict> conflicts = ConflictMarkers::find(text);

        QCOMPARE(conflicts.size(), 1);
        QCOMPARE(conflicts.first().startLine, 2);
        QCOMPARE(conflicts.first().currentLabel, QString("b"));
    }

    void resolvesAConflictByKeepingOneSideOrBoth()
    {
        const QByteArray text = "<<<<<<< HEAD\nmine\n||||||| base\nold\n=======\ntheirs\n>>>>>>> other\nend\n";
        const ConflictMarkers::Conflict conflict = ConflictMarkers::find(text).first();
        const auto resolved = [&](ConflictMarkers::Resolution resolution) {
            const ConflictMarkers::Replacement replacement = ConflictMarkers::resolve(text, conflict, resolution);
            QByteArray result = text;
            return result.replace(replacement.span.start, replacement.span.end - replacement.span.start,
                                  replacement.text);
        };

        QCOMPARE(resolved(ConflictMarkers::Resolution::Current), QByteArray("mine\nend\n"));
        QCOMPARE(resolved(ConflictMarkers::Resolution::Incoming), QByteArray("theirs\nend\n"));
        QCOMPARE(resolved(ConflictMarkers::Resolution::Both), QByteArray("mine\ntheirs\nend\n"));
    }

private:
    QTemporaryFile config_;
};

QTEST_GUILESS_MAIN(TestGit)
#include "tst_git.moc"
