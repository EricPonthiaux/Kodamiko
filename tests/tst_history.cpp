// SPDX-License-Identifier: GPL-3.0-only

#include "changes/ChangeTree.h"
#include "changes/DiffHeaderBar.h"
#include "changes/DiffView.h"
#include "git/CommitLog.h"
#include "git/GitProcess.h"
#include "history/CommitDelegate.h"
#include "history/CommitGraph.h"
#include "history/CommitQuery.h"
#include "history/HistoryView.h"
#include "ui/FoldedPane.h"

#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QScrollBar>
#include <QSplitter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QToolButton>
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
    const std::optional<CommitLog> log = CommitLog::read(folder, {.limit = limit});
    if (!log)
        qFatal("no git repository");
    return *log;
}

QStringList pathsOf(const CommitLog &log)
{
    QStringList result;
    for (const CommitSummary &commit : log.commits)
        result.append(commit.path);
    return result;
}

// Content similar enough across small edits for git to see a rename.
QByteArray longText(const QByteArray &lastLine)
{
    return QByteArray("line\n").repeated(20) + lastLine + "\n";
}

// `count` commits on main, "c0001" the oldest, each changing f.txt; at once,
// through fast-import.
void importCommits(const QTemporaryDir &dir, int count)
{
    QByteArray stream;
    for (int i = 1; i <= count; ++i) {
        const QByteArray number = QByteArray::number(i);
        const QByteArray message = "c" + number.rightJustified(4, '0');
        stream += "commit refs/heads/main\nmark :" + number + "\ncommitter t <t@t> " +
                  QByteArray::number(1700000000 + i) + " +0000\ndata " + QByteArray::number(message.size()) + "\n" +
                  message + "\n";
        if (i > 1)
            stream += "from :" + QByteArray::number(i - 1) + "\n";
        stream += "M 644 inline f.txt\ndata " + QByteArray::number(number.size()) + "\n" + number + "\n";
    }
    QProcess process;
    process.setWorkingDirectory(dir.path());
    process.start("git", {"fast-import", "--quiet"});
    process.write(stream);
    process.closeWriteChannel();
    QVERIFY(process.waitForFinished(30000));
    QCOMPARE(process.exitCode(), 0);
    git(dir, {"reset", "-q", "--hard", "main"});
}

// A commit named `id` with `parents`, for the graph.
CommitSummary node(const QString &id, const QStringList &parents)
{
    CommitSummary commit;
    commit.id = id;
    commit.parentIds = parents;
    return commit;
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
        QCOMPARE(log.commits[1].message, QStringLiteral("second\n\nmessage body"));
        QVERIFY(log.commits[2].parentIds.isEmpty());
        QCOMPARE(log.commits[1].parentIds, QStringList({log.commits[2].id}));
    }

    void graphOfALinearHistoryIsOneLane()
    {
        const QList<GraphRow> rows = CommitGraph::layOut({node("c", {"b"}), node("b", {"a"}), node("a", {})});

        QCOMPARE(CommitGraph::laneCount(rows), 1);
        QVERIFY(rows[0].upper.isEmpty()); // nothing above the newest
        QCOMPARE(rows[0].lower, QList<GraphSegment>({{0, 0, 0}}));
        QCOMPARE(rows[1].upper, QList<GraphSegment>({{0, 0, 0}}));
        QVERIFY(rows[2].lower.isEmpty()); // nothing below the first commit
    }

    void graphOpensALaneForAMergedBranchAndJoinsItAtTheFork()
    {
        const QList<GraphRow> rows =
            CommitGraph::layOut({node("m", {"a", "b"}), node("a", {"base"}), node("b", {"base"}), node("base", {})});

        QCOMPARE(CommitGraph::laneCount(rows), 2);
        QCOMPARE(rows[0].lower, QList<GraphSegment>({{0, 1, 1}, {0, 0, 0}})); // to b, then to a
        QCOMPARE(rows[1].lane, 0);
        QCOMPARE(rows[2].lane, 1);
        QCOMPARE(rows[2].color, 1);
        QCOMPARE(rows[3].lane, 0);
        QCOMPARE(rows[3].upper, QList<GraphSegment>({{0, 0, 0}, {1, 0, 1}})); // both lines join base
    }

    void graphGivesEachBranchTipItsOwnLane()
    {
        const QList<GraphRow> rows = CommitGraph::layOut({node("x", {"base"}), node("y", {"base"}), node("base", {})});

        QCOMPARE(rows[1].lane, 1);
        QCOMPARE(rows[1].upper, QList<GraphSegment>({{0, 0, 0}})); // nothing leads to y
        QCOMPARE(rows[2].upper, QList<GraphSegment>({{0, 0, 0}, {1, 0, 1}}));
    }

    void logOfAllBranches()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "base", {{"a.txt", "a\n"}});
        git(dir, {"checkout", "-q", "-b", "topic"});
        commit(dir, "on topic", {{"b.txt", "b\n"}});
        git(dir, {"checkout", "-q", "main"});
        commit(dir, "on main", {{"c.txt", "c\n"}});

        QCOMPARE(summaries(*CommitLog::read(dir.path())), QStringList({"on main", "base"}));
        QCOMPARE(CommitLog::read(dir.path(), {.scope = LogScope::AllBranches})->commits.size(), 3);
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

        QCOMPARE(summaries(*CommitLog::read(dir.path(), {.limit = 100, .path = "src/a.cpp"})),
                 QStringList({"a modified", "a created"}));
        QCOMPARE(summaries(*CommitLog::read(dir.filePath("src"), {.limit = 100, .path = "b.cpp"})),
                 QStringList({"b created"}));
        QCOMPARE(summaries(*CommitLog::read(dir.path(), {.limit = 100, .path = "src"})).size(), 3);
    }

    void fileHistoryFollowsRenames()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "create", {{"old.txt", longText("1")}});
        commit(dir, "edit", {{"old.txt", longText("2")}, {"other.txt", "o\n"}});
        QVERIFY(QFile::remove(dir.filePath("old.txt")));
        commit(dir, "rename", {{"new.txt", longText("3")}});
        commit(dir, "unrelated", {{"other.txt", "p\n"}});
        commit(dir, "after", {{"new.txt", longText("4")}});

        const std::optional<CommitLog> log = CommitLog::read(dir.path(), {.path = "new.txt"});

        QVERIFY(log);
        QCOMPARE(summaries(*log), QStringList({"after", "rename", "edit", "create"}));
        QCOMPARE(pathsOf(*log), QStringList({"new.txt", "new.txt", "old.txt", "old.txt"}));
        QCOMPARE(pathsOf(*CommitLog::read(dir.path())), QStringList(5, QString())); // no file followed
    }

    void fileHistoryStopsAtARenameFromOutsideTheFolder()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "create", {{"a.txt", longText("1")}});
        QVERIFY(QFile::remove(dir.filePath("a.txt")));
        commit(dir, "move in", {{"src/a.txt", longText("1")}});

        const std::optional<CommitLog> log = CommitLog::read(dir.filePath("src"), {.path = "a.txt"});

        QVERIFY(log);
        QCOMPARE(summaries(*log), QStringList({"move in"})); // added, as far as src sees
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
        QCOMPARE(details.files[0].newText, QByteArray("two\n")); // the commit's version
        QCOMPARE(details.files[1].change, (FileChange{ChangeKind::Renamed, "new.txt", "old.txt"}));

        // First commit: compared with an empty tree.
        const CommitDetails first = CommitDetails::read(dir.path(), log.commits[1].id);
        QVERIFY(first.parentIds.isEmpty());
        QCOMPARE(first.files.size(), 2);
        QCOMPARE(first.files[0].change.kind, ChangeKind::Added);
    }

    void detailsCanIgnoreWhitespace()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "first", {{"a.txt", "a b\n"}});
        commit(dir, "reindent", {{"a.txt", "  a b\n"}});
        const QString id = readLog(dir.path()).commits.first().id;

        const CommitDetails ignoring = CommitDetails::read(dir.path(), id, Whitespace::Ignore);
        const CommitDetails comparing = CommitDetails::read(dir.path(), id);

        QCOMPARE(ignoring.files.size(), 1); // still listed, without blocks
        QVERIFY(!ignoring.files[0].patch.contains("@@"));
        QVERIFY(comparing.files[0].patch.contains("+  a b"));
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

    void viewNamesTheFileItIsFilteredOn()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "a created", {{"src/a.txt", "a\n"}});
        commit(dir, "b created", {{"b.txt", "b\n"}});
        HistoryView view(Theme::duet());
        view.setFolder(dir.path());
        const QSignalSpy refreshed(&view, &HistoryView::refreshed);

        view.setPathFilter("src/a.txt");

        QTRY_VERIFY(!refreshed.isEmpty());
        const auto *commits = view.findChild<QTreeWidget *>("commits");
        QCOMPARE(commits->topLevelItemCount(), 1);
        const QList<QLabel *> labels = view.findChildren<QLabel *>();
        QVERIFY(std::ranges::any_of(labels, [](const QLabel *label) { return label->text() == "History of a.txt"; }));
    }

    void fileHistoryShowsOnlyTheFileDiffOfEachCommit()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "both", {{"a.txt", "a\n"}, {"b.txt", "b\n"}});
        HistoryView view(Theme::duet());
        view.setFolder(dir.path());
        const QSignalSpy refreshed(&view, &HistoryView::refreshed);
        view.setPathFilter("a.txt");
        QTRY_VERIFY(!refreshed.isEmpty());
        auto *commits = view.findChild<QTreeWidget *>("commits");
        const auto *files = view.findChild<QTreeWidget *>("files");
        const auto *diff = view.findChild<DiffView *>();

        commits->setCurrentItem(commits->topLevelItem(0));

        QTRY_VERIFY(diff->text().contains("a.txt"));
        QVERIFY(!diff->text().contains("b.txt"));
        QCOMPARE(files->topLevelItemCount(), 1);
        QVERIFY(!files->isVisibleTo(&view)); // the commit pane: no list to look through
    }

    void hidingWhitespaceKeepsTheChosenFile()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "first", {{"a.txt", "a\n"}, {"b.txt", "b c\n"}});
        commit(dir, "second", {{"a.txt", "z\n"}, {"b.txt", "b   c\n"}});
        HistoryView view(Theme::duet());
        view.setFolder(dir.path());
        const QSignalSpy refreshed(&view, &HistoryView::refreshed);
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 1);
        auto *commits = view.findChild<QTreeWidget *>("commits");
        auto *files = view.findChild<QTreeWidget *>("files");
        const auto *diff = view.findChild<DiffView *>();
        commits->setCurrentItem(commits->topLevelItem(0));
        QTRY_COMPARE(files->topLevelItemCount(), 2);
        files->setCurrentItem(files->topLevelItem(1));
        QTRY_VERIFY(diff->text().contains("b   c"));
        QToolButton *whitespace = nullptr;
        for (QToolButton *button : view.findChild<DiffHeaderBar *>()->findChildren<QToolButton *>()) {
            if (button->toolTip() == QLatin1String("Hide whitespace changes"))
                whitespace = button;
        }
        QVERIFY(whitespace);

        whitespace->click();

        QTRY_VERIFY(diff->text().contains("No changes to the text"));
        QVERIFY(diff->text().contains("b.txt"));
    }

    void fileHistoryShowsTheDiffUnderTheOldName()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "create", {{"old.txt", longText("1")}});
        QVERIFY(QFile::remove(dir.filePath("old.txt")));
        commit(dir, "rename", {{"new.txt", longText("1")}});
        HistoryView view(Theme::duet());
        view.setFolder(dir.path());
        const QSignalSpy refreshed(&view, &HistoryView::refreshed);
        view.setPathFilter("new.txt");
        QTRY_VERIFY(!refreshed.isEmpty());
        auto *commits = view.findChild<QTreeWidget *>("commits");
        QCOMPARE(commits->topLevelItemCount(), 2);
        const auto *diff = view.findChild<DiffView *>();

        commits->setCurrentItem(commits->topLevelItem(1)); // "create"

        QTRY_VERIFY(diff->text().contains("old.txt"));
    }

    void scrollingToTheEndLoadsMoreCommits()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        importCommits(dir, 1200);
        HistoryView view(Theme::duet());
        view.resize(900, 600);
        view.show();
        view.setFolder(dir.path());
        view.refresh();
        auto *commits = view.findChild<QTreeWidget *>("commits");
        QTRY_COMPARE(commits->topLevelItemCount(), 500);
        QCOMPARE(commits->verticalScrollBar()->value(), 0); // nothing more until scrolled

        commits->scrollToBottom();
        QTRY_COMPARE(commits->topLevelItemCount(), 1000);
        QVERIFY(commits->verticalScrollBar()->value() > 0); // where it was, not back at the top
        commits->scrollToBottom();
        QTRY_COMPARE(commits->topLevelItemCount(), 1200); // all of them: no more reads
    }

    void filteringLooksBeyondTheLoadedCommits()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        importCommits(dir, 1200);
        HistoryView view(Theme::duet());
        view.resize(900, 600);
        view.show();
        view.setFolder(dir.path());
        view.refresh();
        auto *commits = view.findChild<QTreeWidget *>("commits");
        QTRY_COMPARE(commits->topLevelItemCount(), 500);

        view.findChild<QLineEdit *>()->setText("c0001");

        QTRY_COMPARE(commits->topLevelItemCount(), 1200);
        QVERIFY(!commits->topLevelItem(1199)->isHidden()); // the oldest, found
    }

    void queryReadsWordsAndTerms()
    {
        const QDate today(2024, 6, 30);

        const CommitQuery query = CommitQuery::parse(
            R"(fix author:ada message:"null check" after:2024-03-01 before:2w contents:"a b" http://x)", today);

        QCOMPARE(query.words, QStringList({"fix", "http://x"}));
        QCOMPARE(query.authors, QStringList({"ada"}));
        QCOMPARE(query.messageTexts, QStringList({"null check"}));
        QCOMPARE(query.after, QDate(2024, 3, 1));
        QCOMPARE(query.before, QDate(2024, 6, 16));
        QCOMPARE(query.contents, (ContentSearch{ContentMatch::Occurrences, "a b"}));
        QVERIFY(query.error.isEmpty());
    }

    void queryIgnoresATermWhoseValueIsBeingTyped()
    {
        const CommitQuery query = CommitQuery::parse("fix author:");

        QCOMPARE(query.words, QStringList({"fix"}));
        QVERIFY(query.authors.isEmpty());
        QVERIFY(query.error.isEmpty());
    }

    void queryExplainsAnUnknownDateAndAnInvalidRegex()
    {
        QVERIFY(CommitQuery::parse("after:yesterday").error.contains("yesterday"));
        const CommitQuery query = CommitQuery::parse("diff:(unclosed");
        QVERIFY(!query.contents);
        QVERIFY(query.error.contains("(unclosed"));
    }

    void queryMatchesTheMessageBodyTheAuthorAndTheDays()
    {
        CommitSummary commit;
        commit.summary = "Fix the parser";
        commit.message = "Fix the parser\n\nAdds a null check.";
        commit.author = "Ada";
        commit.email = "ada@example.org";
        commit.date = QDateTime(QDate(2024, 3, 15), QTime(12, 0));

        QVERIFY(CommitQuery::parse("null check").matches(commit));
        QVERIFY(CommitQuery::parse("message:\"null check\" author:example.org").matches(commit));
        QVERIFY(CommitQuery::parse("after:2024-03-15 before:2024-03-15").matches(commit));
        QVERIFY(!CommitQuery::parse("after:2024-03-16").matches(commit));
        QVERIFY(!CommitQuery::parse("parser author:bob").matches(commit));
    }

    void contentSearchFindsTheCommitsAddingOrRemovingTheText()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "add", {{"a.txt", "x\nneedle\n"}});
        commit(dir, "edit around", {{"a.txt", "y\nneedle\n"}});
        commit(dir, "edit the line", {{"a.txt", "y\nneedle here\n"}});
        commit(dir, "remove", {{"a.txt", "y\n"}});

        const std::optional<CommitLog> log =
            CommitLog::read(dir.path(), {.contents = ContentSearch{ContentMatch::Occurrences, "needle"}});

        QCOMPARE(summaries(*log), QStringList({"remove", "add"}));
    }

    void contentSearchByRegexFindsTheChangedLines()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "add", {{"a.txt", "needle\n"}});
        commit(dir, "edit around", {{"a.txt", "x\nneedle\n"}});
        commit(dir, "edit the line", {{"a.txt", "x\nneedle here\n"}});

        const std::optional<CommitLog> log =
            CommitLog::read(dir.path(), {.contents = ContentSearch{ContentMatch::ChangedLine, "^nee+dle"}});

        // The line edited still holds the text once: found by its change.
        QCOMPARE(summaries(*log), QStringList({"edit the line", "add"}));
    }

    void contentSearchStaysInTheFilteredFile()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "in a", {{"a.txt", "needle\n"}});
        commit(dir, "in b", {{"b.txt", "needle\n"}});

        const std::optional<CommitLog> log = CommitLog::read(
            dir.path(), {.path = "a.txt", .contents = ContentSearch{ContentMatch::Occurrences, "needle"}});

        QCOMPARE(summaries(*log), QStringList({"in a"}));
    }

    void viewFiltersByAuthorAndSearchesTheChanges()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "add", {{"a.txt", "needle\n"}});
        commit(dir, "other", {{"b.txt", "hay\n"}});
        git(dir, {"commit", "-q", "--allow-empty", "-m", "by bob", "--author", "Bob <bob@example.org>"});
        HistoryView view(Theme::duet());
        view.resize(900, 600);
        view.show();
        view.setFolder(dir.path());
        view.refresh();
        auto *commits = view.findChild<QTreeWidget *>("commits");
        QTRY_COMPARE(commits->topLevelItemCount(), 3);
        auto *field = view.findChild<QLineEdit *>();

        field->setText("author:bob");
        QVERIFY(!commits->topLevelItem(0)->isHidden());
        QVERIFY(commits->topLevelItem(1)->isHidden());

        field->setText("contents:needle");
        QTRY_COMPARE(commits->topLevelItemCount(), 1);
        QCOMPARE(commits->topLevelItem(0)->text(0), QStringLiteral("add"));
    }

    void historyDrawsTheGraphOfEveryBranch()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "base", {{"a.txt", "a\n"}});
        git(dir, {"checkout", "-q", "-b", "topic"});
        commit(dir, "on topic", {{"b.txt", "b\n"}});
        git(dir, {"checkout", "-q", "main"});
        commit(dir, "on main", {{"c.txt", "c\n"}});
        HistoryView view(Theme::duet());
        view.resize(900, 600);
        view.show();
        view.setFolder(dir.path());
        const QSignalSpy refreshed(&view, &HistoryView::refreshed);
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 1);
        auto *commits = view.findChild<QTreeWidget *>("commits");
        const auto *delegate = qobject_cast<CommitDelegate *>(commits->itemDelegate());
        QCOMPARE(commits->topLevelItemCount(), 2);
        QCOMPARE(delegate->graphLanes(), 1);
        QToolButton *allBranches = nullptr;
        for (QToolButton *button : view.findChildren<QToolButton *>()) {
            if (button->text() == QLatin1String("All Branches"))
                allBranches = button;
        }
        QVERIFY(allBranches);

        allBranches->click();

        QTRY_COMPARE(commits->topLevelItemCount(), 3);
        QCOMPARE(delegate->graphLanes(), 2);
        view.findChild<QLineEdit *>()->setText("topic");
        QCOMPARE(delegate->graphLanes(), 0); // its lines would cross hidden rows
        view.findChild<QLineEdit *>()->clear();
        QCOMPARE(delegate->graphLanes(), 2);
    }

    void folderHistoryListsOnlyTheFolderFiles()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "all", {{"src/a.txt", "a\n"}, {"src/b.txt", "b\n"}, {"c.txt", "c\n"}});
        HistoryView view(Theme::duet());
        view.setFolder(dir.path());
        const QSignalSpy refreshed(&view, &HistoryView::refreshed);
        view.setPathFilter("src");
        QTRY_VERIFY(!refreshed.isEmpty());
        auto *commits = view.findChild<QTreeWidget *>("commits");
        const auto *files = view.findChild<QTreeWidget *>("files");

        commits->setCurrentItem(commits->topLevelItem(0));

        QTRY_COMPARE(files->topLevelItemCount(), 1);
        QCOMPARE(files->topLevelItem(0)->text(0), QString("src")); // the folder, holding both
        QCOMPARE(ChangeTree::filesUnder(files->topLevelItem(0)).size(), 2);
        QVERIFY(files->isVisibleTo(&view));
    }

    void viewRevealsCommitThenFile()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "first", {{"a.txt", "a\n"}});
        commit(dir, "second", {{"a.txt", "b\n"}, {"b.txt", "b\n"}});

        HistoryView view(Theme::duet());
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
        HistoryView view(Theme::duet());
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

    void foldingTheCommitsNamesTheChosenOneInTheTitleBar()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "first", {{"a.txt", "a\n"}});
        commit(dir, "second", {{"a.txt", "b\n"}});
        HistoryView view(Theme::duet());
        view.resize(1400, 700);
        view.show();
        view.setFolder(dir.path());
        const QSignalSpy refreshed(&view, &HistoryView::refreshed);
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 1);
        auto *commits = view.findChild<QTreeWidget *>("commits");
        commits->setCurrentItem(commits->topLevelItem(1)); // "first"
        const auto titleTexts = [&view] {
            QStringList texts;
            for (const QLabel *label : view.findChild<QFrame *>("viewTitleBar")->findChildren<QLabel *>()) {
                if (label->isVisible())
                    texts.append(label->text());
            }
            return texts;
        };
        QVERIFY(!titleTexts().contains("first"));

        toolButton(view, "Hide the commits")->click();

        QVERIFY(!commits->isVisible());
        QVERIFY(titleTexts().contains("first"));
        toolButton(view, "Show the commits")->click();
        QVERIFY(commits->isVisible());
        QVERIFY(!titleTexts().contains("first"));
    }

    void foldingTheCommitShowsAllItsFilesInTheDiff()
    {
        const QTemporaryDir dir;
        git(dir, {"init", "-q", "-b", "main"});
        commit(dir, "both", {{"a.txt", "a\n"}, {"b.txt", "b\n"}});
        HistoryView view(Theme::duet());
        view.resize(1400, 700);
        view.show();
        view.setFolder(dir.path());
        const QSignalSpy refreshed(&view, &HistoryView::refreshed);
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 1);
        auto *commits = view.findChild<QTreeWidget *>("commits");
        const auto *files = view.findChild<QTreeWidget *>("files");
        const auto *diff = view.findChild<DiffView *>();
        commits->setCurrentItem(commits->topLevelItem(0));
        QTRY_COMPARE(files->topLevelItemCount(), 2);
        QVERIFY(diff->fileSummaries().isEmpty()); // no file chosen yet

        toolButton(view, "Hide the commit and its files")->click();

        QVERIFY(!files->isVisible());
        QCOMPARE(diff->fileSummaries().size(), 2);
        toolButton(view, "Show the commit and its files")->click();
        QVERIFY(files->isVisible());
        QVERIFY(diff->fileSummaries().isEmpty());
    }

    void theFoldedCommitSitsBetweenTheCommitsAndTheDiff_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("wide") << 1400;
        QTest::newRow("narrow") << 900;
    }

    void theFoldedCommitSitsBetweenTheCommitsAndTheDiff()
    {
        QFETCH(const int, width);
        HistoryView view(Theme::duet());
        view.resize(width, 700);
        view.show();
        const auto *commits = view.findChild<QTreeWidget *>("commits");
        const auto *diff = view.findChild<DiffView *>();
        const auto xOf = [&view](const QWidget *widget) { return widget->mapTo(&view, QPoint()).x(); };

        view.setCommitPaneFolded(true);

        const FoldedPane *bar = nullptr;
        for (const FoldedPane *pane : view.findChildren<FoldedPane *>()) {
            if (pane->isVisible())
                bar = pane;
        }
        QVERIFY(bar);
        QTRY_VERIFY(xOf(bar) >= xOf(commits) + commits->width());
        QVERIFY(xOf(diff) >= xOf(bar) + bar->width());
    }

    void foldingAPaneWidensOnlyTheDiff()
    {
        HistoryView view(Theme::duet());
        view.resize(1400, 700);
        view.show();
        QTRY_VERIFY(view.isWide());
        const auto *commits = view.findChild<QTreeWidget *>("commits");
        const auto *diff = view.findChild<DiffView *>();
        const int commitsWidth = commits->width();
        const int diffWidth = diff->width();

        view.setCommitPaneFolded(true);

        QTRY_VERIFY(diff->width() > diffWidth + 100);
        QCOMPARE(commits->width(), commitsWidth);
    }

    void foldedButtonsLineUpWithThoseOfThePaneBars()
    {
        HistoryView view(Theme::duet());
        view.resize(1400, 700);
        view.show();
        const QToolButton *hide = toolButton(view, "Hide the commits");
        const auto centerY = [&view](const QWidget *widget) {
            return widget->mapTo(&view, widget->rect().center()).y();
        };

        view.setCommitPaneFolded(true);

        QTRY_VERIFY(std::abs(centerY(toolButton(view, "Show the commit and its files")) - centerY(hide)) <= 1);
    }

    void foldsAreKeptInTheSavedState()
    {
        HistoryView folded(Theme::duet());
        folded.setCommitListFolded(true);
        folded.setCommitPaneFolded(true);
        HistoryView restored(Theme::duet());

        restored.restoreState(folded.saveState());

        QVERIFY(restored.isCommitListFolded());
        QVERIFY(restored.isCommitPaneFolded());
    }

    void emptyDiffHintUsesThePaneWidthAndWraps_data()
    {
        QTest::addColumn<int>("paneWidth");
        QTest::addColumn<int>("fontSize");
        QTest::newRow("normal") << 420 << 11;
        QTest::newRow("narrow") << 220 << 11;
        QTest::newRow("large-font") << 420 << 18;
        QTest::newRow("narrow-large-font") << 220 << 18;
    }

    void emptyDiffHintUsesThePaneWidthAndWraps()
    {
        QFETCH(const int, paneWidth);
        QFETCH(const int, fontSize);
        HistoryView view(Theme::duet());
        QFont font = view.font();
        font.setPointSize(fontSize);
        view.setFont(font);
        view.resize(1100, 600);
        view.show();
        auto *header = view.findChild<DiffHeaderBar *>();
        auto *splitter = qobject_cast<QSplitter *>(header->parentWidget()->parentWidget());
        QVERIFY(splitter);
        header->setPlaceholder("Choose a file of the commit");
        splitter->setSizes({1100 - paneWidth, paneWidth});
        const auto *hint = header->findChild<QLabel *>("historyMuted");
        QVERIFY(hint);

        // Invisible file controls must leave their width to the whole hint.
        QTRY_VERIFY(hint->width() >= header->width() - 32);
        QTRY_VERIFY(hint->height() >= hint->heightForWidth(hint->width()));
        QVERIFY(hint->wordWrap());
        QVERIFY(header->rect().contains(hint->geometry()));

        // A real file, then an empty selection again: no stale counts or tooltip.
        auto *diff = view.findChild<DiffView *>();
        diff->setFiles({{"src/a.cpp", "modified", FilePatch::parse("@@ -1 +1 @@\n-a\n+b\n")}});
        diff->showMessage({});
        QTRY_VERIFY(hint->width() >= header->width() - 32);
        QTRY_VERIFY(hint->height() >= hint->heightForWidth(hint->width()));
        QCOMPARE(hint->toolTip(), hint->text());
    }

private:
    // The tool button of `view` that says `toolTip`.
    static QToolButton *toolButton(const QWidget &view, const QString &toolTip)
    {
        for (QToolButton *button : view.findChildren<QToolButton *>()) {
            if (button->toolTip() == toolTip)
                return button;
        }
        return nullptr;
    }

    QTemporaryFile config_;
};

QTEST_MAIN(TestHistory)
#include "tst_history.moc"
