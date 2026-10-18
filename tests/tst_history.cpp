#include "changes/DiffView.h"
#include "git/CommitLog.h"
#include "git/GitProcess.h"
#include "history/HistoryView.h"

#include <QLabel>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTreeWidget>
#include <QtTest>

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

// Writes `files` and commits them with the message `message`.
void commit(const QTemporaryDir &dir, const QString &message, const QHash<QString, QByteArray> &files)
{
    for (auto it = files.begin(); it != files.end(); ++it)
        write(dir, it.key(), it.value());
    git(dir, {"add", "-A"});
    git(dir, {"commit", "-qm", message});
}

CommitLog readLog(const QString &folder, int limit = 2000)
{
    const std::optional<CommitLog> log = CommitLog::read(folder, limit);
    if (!log)
        qFatal("no git repository");
    return *log;
}

QStringList summaries(const CommitLog &log)
{
    QStringList result;
    for (const CommitSummary &commit : log.commits)
        result.append(commit.summary);
    return result;
}

} // namespace

class TestHistory : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("git unavailable");
        // Neither the user's configuration (commit signing…) nor
        // the system's.
        QVERIFY(config_.open());
        qputenv("GIT_CONFIG_GLOBAL", config_.fileName().toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_AUTHOR_NAME", "Ada");
        qputenv("GIT_AUTHOR_EMAIL", "ada@example.org");
        qputenv("GIT_COMMITTER_NAME", "Ada");
        qputenv("GIT_COMMITTER_EMAIL", "ada@example.org");
    }

    void logOutsideARepository()
    {
        const QTemporaryDir dir;
        QVERIFY(!CommitLog::read(dir.path()));
    }

    void logOfAnEmptyRepository()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        const CommitLog log = readLog(dir.path());
        QVERIFY(log.info.unborn);
        QVERIFY(log.commits.isEmpty());
    }

    void logNewestFirstWithRefs()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "first", {{"a.txt", "a\n"}});
        commit(dir, "second\n\nmessage body", {{"a.txt", "b\n"}});
        git(dir, {"tag", "v1"});
        commit(dir, "third", {{"b.txt", "c\n"}});

        const CommitLog log = readLog(dir.path());
        QCOMPARE(summaries(log), QStringList({"third", "second", "first"}));
        QVERIFY(!log.truncated);
        const CommitSummary &newest = log.commits.first();
        QCOMPARE(newest.author, QStringLiteral("Ada"));
        QCOMPARE(newest.email, QStringLiteral("ada@example.org"));
        QCOMPARE(newest.id.size(), 40);
        QCOMPARE(newest.shortId, newest.id.left(7));
        QCOMPARE(newest.refs, QStringList({"main"}));
        QCOMPARE(log.commits[1].refs, QStringList({"v1"}));
        QCOMPARE(log.commits[2].parentCount, 0);
    }

    void logMarksADetachedHead()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "first", {{"a.txt", "a\n"}});
        commit(dir, "second", {{"a.txt", "b\n"}});
        git(dir, {"checkout", "-q", "HEAD~1"});

        const CommitLog log = readLog(dir.path());
        QCOMPARE(summaries(log), QStringList({"first"}));
        QCOMPARE(log.commits.first().refs, QStringList({"HEAD"}));
    }

    void logIsLimited()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        for (int i = 0; i < 4; ++i)
            commit(dir, QString::number(i), {{"a.txt", QByteArray::number(i)}});
        const CommitLog log = readLog(dir.path(), 3);
        QCOMPARE(summaries(log), QStringList({"3", "2", "1"}));
        QVERIFY(log.truncated);
    }

    void logOfASubfolderKeepsItsCommits()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "root", {{"README", "r\n"}});
        commit(dir, "src created", {{"src/a.cpp", "a\n"}});
        commit(dir, "doc", {{"doc/x.md", "x\n"}});
        commit(dir, "src modified", {{"src/a.cpp", "b\n"}});

        const CommitLog log = readLog(dir.filePath("src"));
        QCOMPARE(log.info.prefix, QStringLiteral("src/"));
        QCOMPARE(summaries(log), QStringList({"src modified", "src created"}));
    }

    void logOfAFileKeepsItsCommits()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "a created", {{"src/a.cpp", "a\n"}});
        commit(dir, "b created", {{"src/b.cpp", "b\n"}});
        commit(dir, "a modified", {{"src/a.cpp", "aa\n"}});

        QCOMPARE(summaries(*CommitLog::read(dir.path(), 100, "src/a.cpp")), QStringList({"a modified", "a created"}));
        QCOMPARE(summaries(*CommitLog::read(dir.filePath("src"), 100, "b.cpp")), QStringList({"b created"}));
        QCOMPARE(summaries(*CommitLog::read(dir.path(), 100, "src")).size(), 3);
    }

    void detailsOfACommit()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "first", {{"a.txt", "one\n"}, {"old.txt", "content long enough for a rename\n"}});
        QVERIFY(QFile::remove(dir.filePath("old.txt")));
        commit(dir, "second\n\ndetails", {{"a.txt", "two\n"}, {"new.txt", "content long enough for a rename\n"}});

        const CommitLog log = readLog(dir.path());
        const CommitDetails details = CommitDetails::read(dir.path(), log.commits.first().id);
        QCOMPARE(details.summary.summary, QStringLiteral("second"));
        QCOMPARE(details.message, QStringLiteral("second\n\ndetails"));
        QVERIFY(details.committer.isEmpty());
        QCOMPARE(details.parentIds, QStringList({log.commits[1].shortId}));
        QCOMPARE(details.files.size(), 2);
        QCOMPARE(details.files[0].change, (FileChange{ChangeKind::Modified, "a.txt", {}}));
        QVERIFY(details.files[0].patch.contains("-one\n+two\n"));
        QCOMPARE(details.files[1].change, (FileChange{ChangeKind::Renamed, "new.txt", "old.txt"}));

        // First commit: compared with an empty tree.
        const CommitDetails first = CommitDetails::read(dir.path(), log.commits[1].id);
        QVERIFY(first.parentIds.isEmpty());
        QCOMPARE(first.files.size(), 2);
        QCOMPARE(first.files[0].change.kind, ChangeKind::Added);
    }

    void detailsOfASubfolderAreRelative()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "everything", {{"src/a.cpp", "a\n"}, {"README", "r\n"}});

        const CommitLog log = readLog(dir.filePath("src"));
        const CommitDetails details = CommitDetails::read(dir.filePath("src"), log.commits.first().id);
        QCOMPARE(details.files.size(), 1);
        QCOMPARE(details.files[0].change.path, QStringLiteral("a.cpp"));
    }

    void viewRevealsCommitThenFile()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "first", {{"a.txt", "a\n"}});
        commit(dir, "second", {{"a.txt", "b\n"}, {"b.txt", "b\n"}});

        HistoryView view(Theme::dark());
        view.setFolder(dir.path());
        const QSignalSpy refreshed(&view, &HistoryView::refreshed);
        view.refresh(); // off the UI thread
        QTRY_COMPARE(refreshed.size(), 1);
        QCOMPARE(view.findChildren<QTreeWidget *>().size(), 2);
        auto *commits = view.findChild<QTreeWidget *>("commits");
        auto *files = view.findChild<QTreeWidget *>("files");
        QVERIFY(commits && files);
        const auto *diff = view.findChild<DiffView *>();

        // Initially, every pane in place, with no commit chosen.
        QCOMPARE(commits->topLevelItemCount(), 2);
        QVERIFY(commits->selectedItems().isEmpty());
        QVERIFY(!files->isVisibleTo(&view)); // a hint in its place
        QVERIFY(diff->isVisibleTo(&view));
        QVERIFY(diff->text().isEmpty());

        // A commit chosen: its message and files, no diff yet.
        commits->setCurrentItem(commits->topLevelItem(0));
        QTRY_COMPARE(files->topLevelItemCount(), 2);
        QVERIFY(files->isVisibleTo(&view));
        const QStringList labels = [&view] {
            QStringList texts;
            for (const QLabel *label : view.findChildren<QLabel *>())
                texts.append(label->text());
            return texts;
        }();
        QVERIFY(labels.contains("second"));
        QVERIFY(diff->text().isEmpty());

        // A file chosen: its diff, and only that one.
        files->setCurrentItem(files->topLevelItem(1));
        QVERIFY(diff->text().contains("b.txt"));
        QVERIFY(!diff->text().contains("a.txt"));

        // Another commit: the diff empties until a file is chosen.
        commits->setCurrentItem(commits->topLevelItem(1));
        QTRY_COMPARE(files->topLevelItemCount(), 1);
        QVERIFY(diff->text().isEmpty());

        // A new commit: the selection stays on the chosen commit.
        commit(dir, "third", {{"c.txt", "c\n"}});
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 2);
        QCOMPARE(commits->topLevelItemCount(), 3);
        QCOMPARE(commits->currentItem()->text(0), QStringLiteral("first"));
        QCOMPARE(files->topLevelItemCount(), 1);
        QVERIFY(files->isVisibleTo(&view));
    }

    void widePanesStandSideBySide()
    {
        HistoryView view(Theme::dark());
        view.resize(1400, 700);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(view.isWide());
        const auto *files = view.findChild<QTreeWidget *>("files");
        const auto *commits = view.findChild<QTreeWidget *>("commits");
        // Side by side: the commit pane starts right of the commits.
        QVERIFY(files->mapTo(&view, QPoint()).x() > commits->mapTo(&view, QPoint()).x() + commits->width() - 1);

        view.resize(900, 700);
        QTRY_VERIFY(!view.isWide());
    }

private:
    QTemporaryFile config_;
};

QTEST_MAIN(TestHistory)
#include "tst_history.moc"
