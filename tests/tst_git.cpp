#include "git/FilePatch.h"
#include "git/GitCommands.h"
#include "git/GitOperations.h"
#include "git/GitProcess.h"
#include "git/RepositoryStatus.h"
#include "git/Snapshots.h"

#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>

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

// Contenu d'un fichier dans l'index.
QByteArray indexed(const QTemporaryDir &dir, const QString &path)
{
    return git(dir, {"show", ":" + path});
}

// Dépôt avec un premier commit contenant `files`.
void initRepository(const QTemporaryDir &dir, const QHash<QString, QByteArray> &files)
{
    for (auto it = files.begin(); it != files.end(); ++it)
        write(dir, it.key(), it.value());
    git(dir, {"init", "-q", "-b", "main"});
    if (!files.isEmpty()) {
        git(dir, {"add", "-A"});
        git(dir, {"commit", "-qm", "init"});
    }
}

RepositoryStatus status(const QTemporaryDir &dir)
{
    const std::optional<RepositoryStatus> result = RepositoryStatus::read(dir.path());
    if (!result)
        qFatal("pas de dépôt git");
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

// Lignes « + » / « - » du patch dont le texte vérifie `accept`.
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
            QSKIP("git indisponible");
        // Ni la configuration de l'utilisateur (signature des commits…) ni
        // celle du système.
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
        QTemporaryDir project;
        write(project, "a.txt", "a\n");
        QVERIFY(!RepositoryStatus::read(project.path()));
        QVERIFY(!RepositoryInfo::read(project.path()));
    }

    void statusSeparatesStagedAndUnstaged()
    {
        QTemporaryDir project;
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

    void statusOfASubfolderUsesRelativePaths()
    {
        QTemporaryDir project;
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
        QTemporaryDir project;
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
        QTemporaryDir project;
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
        // Le second bloc seul : sa ligne de contexte n'appartient pas au premier.
        QCOMPARE(patch.partial(patch.selectHunk(1), PatchDirection::Forward),
                 QByteArray("diff --git a/f b/f\nindex 1..2 100644\n--- a/f\n+++ b/f\n@@ -10,1 +10,1 @@\n-x\n+X\n"));
    }

    void stageSelectedLines()
    {
        QTemporaryDir project;
        initRepository(project, {{"f.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n"}});
        write(project, "f.txt", "1\nTWO\n3\n4\n5\n6\n7\n8\n9\n10\nELEVEN\n12\nthirteen\n");

        RepositoryStatus result = status(project);
        const FilePatch patch = FilePatch::parse(result.unstaged.first().patch);
        QCOMPARE(patch.hunks.size(), 2);
        // Seulement les lignes du second bloc qui touchent « 11 », pas l'ajout final.
        const PatchSelection selection = selectLines(patch, [](const QByteArray &line) {
            return line == "-11" || line == "+ELEVEN";
        });
        GitOperations::applyToIndex(result.info, patch.partial(selection, PatchDirection::Forward), PatchDirection::Forward);
        QCOMPARE(indexed(project, "f.txt"), QByteArray("1\n2\n3\n4\n5\n6\n7\n8\n9\n10\nELEVEN\n12\n"));

        // Puis le premier bloc : son début ne dépend pas de ce qui est déjà indexé.
        result = status(project);
        const FilePatch rest = FilePatch::parse(result.unstaged.first().patch);
        GitOperations::applyToIndex(result.info, rest.partial(rest.selectHunk(0), PatchDirection::Forward),
                                    PatchDirection::Forward);
        QCOMPARE(indexed(project, "f.txt"), QByteArray("1\nTWO\n3\n4\n5\n6\n7\n8\n9\n10\nELEVEN\n12\n"));
        QCOMPARE(read(project, "f.txt"), QByteArray("1\nTWO\n3\n4\n5\n6\n7\n8\n9\n10\nELEVEN\n12\nthirteen\n"));
    }

    void unstageSelectedLines()
    {
        QTemporaryDir project;
        initRepository(project, {{"f.txt", "a\nb\nc\n"}});
        write(project, "f.txt", "A\nb\nC\nd\n");
        git(project, {"add", "f.txt"});

        const RepositoryStatus result = status(project);
        const FilePatch patch = FilePatch::parse(result.staged.first().patch);
        const PatchSelection selection = selectLines(patch, [](const QByteArray &line) {
            return line == "-c" || line == "+C";
        });
        GitOperations::applyToIndex(result.info, patch.partial(selection, PatchDirection::Reverse), PatchDirection::Reverse);
        QCOMPARE(indexed(project, "f.txt"), QByteArray("A\nb\nc\nd\n"));
        QCOMPARE(read(project, "f.txt"), QByteArray("A\nb\nC\nd\n"));
    }

    void partialChangesOfCreatedAndDeletedFiles()
    {
        QTemporaryDir project;
        initRepository(project, {{"old.txt", "x\ny\nz\n"}});
        write(project, "new.txt", "1\n2\n3\n");
        QFile::remove(project.filePath("old.txt"));

        // Une partie d'un fichier non suivi : il est créé dans l'index.
        RepositoryStatus result = status(project);
        FilePatch created = FilePatch::parse(find(result.unstaged, "new.txt")->patch);
        GitOperations::applyToIndex(result.info,
                                    created.partial(selectLines(created, [](const QByteArray &line) { return line != "+2"; }),
                                                    PatchDirection::Forward),
                                    PatchDirection::Forward);
        QCOMPARE(indexed(project, "new.txt"), QByteArray("1\n3\n"));

        // Une partie d'une suppression : le fichier reste dans l'index.
        const FilePatch deleted = FilePatch::parse(find(result.unstaged, "old.txt")->patch);
        GitOperations::applyToIndex(result.info,
                                    deleted.partial(selectLines(deleted, [](const QByteArray &line) { return line == "-y"; }),
                                                    PatchDirection::Forward),
                                    PatchDirection::Forward);
        QCOMPARE(indexed(project, "old.txt"), QByteArray("x\nz\n"));

        // Retirer une partie d'un fichier créé : il reste dans l'index.
        result = status(project);
        created = FilePatch::parse(find(result.staged, "new.txt")->patch);
        GitOperations::applyToIndex(result.info,
                                    created.partial(selectLines(created, [](const QByteArray &line) { return line == "+3"; }),
                                                    PatchDirection::Reverse),
                                    PatchDirection::Reverse);
        QCOMPARE(indexed(project, "new.txt"), QByteArray("1\n"));
    }

    void stageLinesWithoutFinalNewlineAndCrlf()
    {
        QTemporaryDir project;
        initRepository(project, {{"crlf.txt", "a\r\nb\r\nc\r\n"}, {"eof.txt", "a\nb"}});
        write(project, "crlf.txt", "A\r\nb\r\nC\r\n");
        write(project, "eof.txt", "a\nB");

        RepositoryStatus result = status(project);
        const FilePatch crlf = FilePatch::parse(find(result.unstaged, "crlf.txt")->patch);
        GitOperations::applyToIndex(
            result.info,
            crlf.partial(selectLines(crlf, [](const QByteArray &line) { return line.startsWith("-c") || line.startsWith("+C"); }),
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
        QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}, {"b.txt", "b\n"}});
        write(project, "a.txt", "A\n");
        QFile::remove(project.filePath("b.txt"));
        write(project, "c.txt", "c\n");

        RepositoryInfo info = *RepositoryInfo::read(project.path());
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
        QTemporaryDir project;
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
        QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        write(project, "a.txt", "b\n");
        RepositoryInfo info = *RepositoryInfo::read(project.path());
        GitOperations::stage(info, {"a.txt"});

        const QString id = GitOperations::commit(info, "#12 premier\n\ndétails", false);
        info = *RepositoryInfo::read(project.path());
        QCOMPARE(info.headId, id);
        QCOMPARE(info.headMessage, QString("#12 premier\n\ndétails\n"));
        QVERIFY(status(project).staged.isEmpty());

        GitOperations::commit(info, "reformulé", true);
        QCOMPARE(QString::fromUtf8(git(project, {"rev-list", "--count", "HEAD"})).trimmed(), QString("2"));
        QCOMPARE(RepositoryInfo::read(project.path())->headMessage, QString("reformulé\n"));
    }

    void commitRunsTheRepositoryHooks()
    {
#ifdef Q_OS_WIN
        QSKIP("hook shell");
#endif
        QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        write(project, ".git/hooks/pre-commit", "#!/bin/sh\necho 'refusé par le hook' >&2\nexit 1\n");
        QFile::setPermissions(project.filePath(".git/hooks/pre-commit"),
                              QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        write(project, "a.txt", "b\n");
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        GitOperations::stage(info, {"a.txt"});
        try {
            GitOperations::commit(info, "message", false);
            QFAIL("le hook aurait dû refuser le commit");
        } catch (const GitError &error) {
            QVERIFY(QString::fromStdString(error.what()).contains("refusé par le hook"));
        }
    }

    void discardKeepsASnapshot()
    {
        QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}, {"b.txt", "b\n"}});
        write(project, "a.txt", "modifié\n");
        write(project, "b.txt", "indexé\n");
        git(project, {"add", "b.txt"});
        write(project, "b.txt", "indexé puis modifié\n");
        write(project, "dir/untracked.txt", "non suivi\n");

        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        GitOperations::discard(info, {"a.txt", "b.txt", "dir/untracked.txt"});
        QCOMPARE(read(project, "a.txt"), QByteArray("a\n"));
        QCOMPARE(read(project, "b.txt"), QByteArray("indexé\n")); // version de l'index
        QVERIFY(!QFileInfo::exists(project.filePath("dir")));
        QCOMPARE(indexed(project, "b.txt"), QByteArray("indexé\n"));

        QList<Snapshot> snapshots = Snapshots::list(project.path());
        QCOMPARE(snapshots.size(), 1);
        QCOMPARE(snapshots.first().paths, QStringList({"a.txt", "b.txt", "dir/untracked.txt"}));
        // Hors des branches : l'historique n'a pas bougé.
        QCOMPARE(QString::fromUtf8(git(project, {"rev-list", "--count", "HEAD"})).trimmed(), QString("1"));

        Snapshots::restore(info, snapshots.first());
        QCOMPARE(read(project, "a.txt"), QByteArray("modifié\n"));
        QCOMPARE(read(project, "b.txt"), QByteArray("indexé puis modifié\n"));
        QCOMPARE(read(project, "dir/untracked.txt"), QByteArray("non suivi\n"));
        QCOMPARE(indexed(project, "b.txt"), QByteArray("indexé\n"));
        // La restauration elle-même est réversible.
        snapshots = Snapshots::list(project.path());
        QCOMPARE(snapshots.size(), 2);
        QVERIFY(snapshots.first().reason.startsWith("Avant la restauration"));
    }

    void discardSelectedLines()
    {
        QTemporaryDir project;
        initRepository(project, {{"f.txt", "a\nb\nc\n"}});
        write(project, "f.txt", "A\nb\nC\n");

        const RepositoryStatus result = status(project);
        const FilePatch patch = FilePatch::parse(result.unstaged.first().patch);
        const PatchSelection selection = selectLines(patch, [](const QByteArray &line) {
            return line == "-a" || line == "+A";
        });
        GitOperations::discardPatch(result.info, {"f.txt"}, patch.partial(selection, PatchDirection::Reverse));
        QCOMPARE(read(project, "f.txt"), QByteArray("a\nb\nC\n"));
        QCOMPARE(Snapshots::list(project.path()).size(), 1);
    }

    void snapshotsArePruned()
    {
        QTemporaryDir project;
        initRepository(project, {{"a.txt", "a\n"}});
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        for (int i = 0; i < Snapshots::maximumCount + 2; ++i)
            Snapshots::create(info, QString::number(i), {"a.txt"});
        const QList<Snapshot> snapshots = Snapshots::list(project.path());
        QCOMPARE(snapshots.size(), Snapshots::maximumCount);
        QCOMPARE(snapshots.first().reason, QString::number(Snapshots::maximumCount + 1));
    }

    void commandsRunInOrderAndReportErrors()
    {
        GitCommands commands;
        QSignalSpy succeeded(&commands, &GitCommands::succeeded);
        QSignalSpy failed(&commands, &GitCommands::failed);
        QSignalSpy busy(&commands, &GitCommands::busyChanged);
        QStringList order;
        bool callbackRan = false;
        commands.run("un", [&order] { order << "un"; return QString("fait"); }, [&callbackRan] { callbackRan = true; });
        commands.run("deux", []() -> QString { throw GitError("échec"); });
        QVERIFY(commands.isBusy());
        QTRY_COMPARE(busy.size(), 2);
        QVERIFY(!commands.isBusy());
        QVERIFY(callbackRan);
        QCOMPARE(succeeded.size(), 1);
        QCOMPARE(succeeded.first().at(1).toString(), QString("fait"));
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.first().at(1).toString(), QString("échec"));
    }

private:
    QTemporaryFile config_;
};

QTEST_GUILESS_MAIN(TestGit)
#include "tst_git.moc"
