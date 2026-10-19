#include "changes/BranchesPopup.h"
#include "changes/ChangesView.h"
#include "changes/DiffHeaderBar.h"
#include "changes/DiffView.h"
#include "changes/FileChangeDelegate.h"
#include "changes/GitActions.h"
#include "changes/HunkActions.h"
#include "changes/NewBranchPopup.h"
#include "changes/SnapshotsPopup.h"
#include "changes/StashesPopup.h"
#include "claude/CommitMessage.h"
#include "git/GitCommands.h"
#include "git/GitProcess.h"
#include "git/RepositoryStatus.h"
#include "git/Snapshots.h"
#include "git/Stashes.h"
#include "ui/PopupWidgets.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
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

// The Commit button, among the view's others.
QPushButton *commitButton(const QWidget &view)
{
    for (QPushButton *button : view.findChildren<QPushButton *>()) {
        if (button->text() == QLatin1String("Commit"))
            return button;
    }
    return nullptr;
}

QStringList sectionTitles(const QTreeWidget *list)
{
    QStringList titles;
    for (int i = 0; i < list->topLevelItemCount(); ++i)
        titles << QStringLiteral("%1 (%2)")
                      .arg(list->topLevelItem(i)->text(0))
                      .arg(list->topLevelItem(i)->data(0, FileChangeDelegate::sectionCountRole).toInt());
    return titles;
}

} // namespace

class TestChanges : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("git unavailable");
        QVERIFY(config_.open());
        qputenv("GIT_CONFIG_GLOBAL", config_.fileName().toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_AUTHOR_NAME", "t");
        qputenv("GIT_AUTHOR_EMAIL", "t@t");
        qputenv("GIT_COMMITTER_NAME", "t");
        qputenv("GIT_COMMITTER_EMAIL", "t@t");
    }

    void diffLayoutDropsGitNotation()
    {
        const FilePatch patch = FilePatch::parse("diff --git a/x.cpp b/x.cpp\n"
                                                 "index 1..2 100644\n"
                                                 "--- a/x.cpp\n"
                                                 "+++ b/x.cpp\n"
                                                 "@@ -10,3 +10,3 @@ void run()\n"
                                                 " keep\n"
                                                 "-old\n"
                                                 "+new\n"
                                                 " end\n"
                                                 "@@ -40 +40,0 @@\n"
                                                 "-gone\n"
                                                 "\\ No newline at end of file\n");
        const QList<DiffLine> lines = DiffLayout::lines({{"x.cpp", "modified", patch}});
        QStringList texts;
        for (const DiffLine &line : lines)
            texts.append(line.text);
        QCOMPARE(texts, QStringList({"", "x.cpp   modified   +1 −2", "⋯ lines 10–12  ·  void run()", "keep", "old",
                                     "new", "end", "⋯ line 40", "gone", "No line break at the end of the file"}));
        QCOMPARE(lines[0].kind, DiffLineKind::Spacer);
        QCOMPARE(lines[1].kind, DiffLineKind::FileHeader);
        QCOMPARE(lines[1].emphasis, 5);    // "x.cpp"
        QCOMPARE(lines[1].statsStart, 19); // "+1 −2"
        QCOMPARE(lines[4].kind, DiffLineKind::Removed);
        QCOMPARE(lines[4].oldNumber, 11);
        QCOMPARE(lines[4].newNumber, 0);
        QCOMPARE(lines[5].kind, DiffLineKind::Added);
        QCOMPARE(lines[5].newNumber, 11);
        QCOMPARE(lines[6].oldNumber, 12);
        QCOMPARE(lines[6].newNumber, 12);
        // Each line leads back to its place in the patch, for staging.
        QCOMPARE(lines[5].location.hunk, 0);
        QCOMPARE(lines[5].location.line, 2);
        QVERIFY(patch.hunks[0].isChange(lines[5].location.line));
        QCOMPARE(lines[2].location.line, -1);
        QCOMPARE(lines[1].location.hunk, -1);
        QCOMPARE(lines[0].location.file, -1); // a blank line leads nowhere

        DiffView view(Theme::dark());
        view.setFiles(
            {{"src/x.cpp", "modified", patch}, {"y.bin", "added", FilePatch::parse("Binary files differ\n")}});
        QCOMPARE(view.displayedLines()[1].text, QString("x.cpp   src  ·  modified   +1 −2"));
        QCOMPARE(view.displayedLines().size(), lines.size() + 3);
        QCOMPARE(view.fileStart(1), static_cast<int>(lines.size()) + 1);
        QCOMPARE(view.locate(5).line, 2);
        QVERIFY(view.text().contains("Binary file"));
        QVERIFY(!view.text().contains("@@"));
    }

    void diffSaysAFileIsTooLargeToShow()
    {
        DiffView view(Theme::dark());

        view.setFiles({{"dump.log", "added", {}, true}});

        QCOMPARE(view.displayedLines().last().text, QString("File too large to show"));
        QCOMPARE(view.displayedLines().last().kind, DiffLineKind::Note);
    }

    void diffShowsChangedWords()
    {
        using Range = TextRange;
        // One word replaced: only it stands out, on both sides.
        const auto [before, after] = WordDiff::changes("int count = 0;", "int total = 0;");
        QCOMPARE(before, QList<Range>({{4, 5}}));
        QCOMPARE(after, QList<Range>({{4, 5}}));
        // Two neighbouring words: one range, the space between them with them.
        QCOMPARE(WordDiff::changes("call(a, b)", "call(x y, b)").second, QList<Range>({{5, 3}}));
        // Rewritten: nothing stands out.
        QVERIFY(WordDiff::changes("return value;", "throw Error(\"no\");").first.isEmpty());

        // Removed then added lines face each other in the layout.
        const FilePatch patch = FilePatch::parse("@@ -1,2 +1,2 @@\n-int count = 0;\n-gone\n+int total = 0;\n");
        const QList<DiffLine> lines = DiffLayout::lines({{"x.cpp", "", patch}});
        QCOMPARE(lines[3].changes, QList<Range>({{4, 5}}));
        QVERIFY(lines[4].changes.isEmpty());
        QCOMPARE(lines[5].changes, QList<Range>({{4, 5}}));
    }

    void diffHidesHeadingsOfProse()
    {
        const FilePatch patch = FilePatch::parse("@@ -10 +10 @@ a sentence cut\n-a\n+b\n");
        QCOMPARE(DiffLayout::lines({{"x.md", "", patch}})[2].text, QString("⋯ line 10"));
        QCOMPARE(DiffLayout::lines({{"x.cpp", "", patch}})[2].text, QString("⋯ line 10  ·  a sentence cut"));
    }

    void diffHeaderFollowsTheFileAtTheTop()
    {
        const QByteArray patch = "@@ -1,2 +1,2 @@\n-a\n+b\n c\n";
        const QList<DiffFile> files = {{"src/one.cpp", "modified", FilePatch::parse(patch)},
                                       {"src/two.cpp", "modified", FilePatch::parse(patch)}};
        DiffView diff(Theme::dark());
        DiffHeaderBar header(&diff);
        diff.resize(600, 30); // a line or two: the second file needs scrolling
        diff.show();
        diff.setFiles(files);
        const auto texts = [&header] {
            QStringList all;
            for (const QLabel *label : header.findChildren<QLabel *>())
                all.append(label->text());
            return all;
        };
        QVERIFY(texts().contains("one.cpp"));
        diff.setFirstVisibleLine(diff.fileStart(1));
        QTRY_VERIFY(texts().contains("two.cpp")); // Scintilla tells once it has painted
        QVERIFY(texts().join(' ').contains("file 2 of 2"));
    }

    void goToChangeScrollsBlockByBlock()
    {
        const QByteArray patch = "@@ -1 +1 @@\n-a\n+b\n@@ -40 +40 @@\n-c\n+d\n";
        DiffView diff(Theme::dark());
        diff.resize(600, 40);
        diff.show();
        diff.setFiles({{"x.txt", "modified", FilePatch::parse(patch)}});
        const QList<DiffLine> &lines = diff.displayedLines();
        QList<int> blocks;
        for (int i = 0; i < lines.size(); ++i) {
            if (lines[i].kind == DiffLineKind::HunkHeader)
                blocks.append(i);
        }
        QCOMPARE(blocks.size(), 2);
        diff.goToChange(true);
        QCOMPARE(diff.firstVisibleLine(), blocks[0]);
        diff.goToChange(true);
        QCOMPARE(diff.firstVisibleLine(), blocks[1]);
        diff.goToChange(false);
        QCOMPARE(diff.firstVisibleLine(), blocks[0]);
    }

    void blockButtonsActOnTheBlockUnderTheMouse()
    {
        const QByteArray patch = "@@ -1 +1 @@\n-a\n+b\n";
        DiffView diff(Theme::dark());
        const HunkActions actions(&diff);
        diff.resize(600, 300);
        diff.show();
        QVERIFY(QTest::qWaitForWindowExposed(&diff));
        diff.setFiles({{"x.txt", "modified", FilePatch::parse(patch)}});
        int block = -1;
        for (int i = 0; i < diff.displayedLines().size(); ++i) {
            if (diff.displayedLines()[i].kind == DiffLineKind::Added)
                block = diff.hunkStart(i);
        }
        QVERIFY(block >= 0);
        const QSignalSpy applied(&actions, &HunkActions::applyRequested);
        const QPoint over(100, diff.lineTop(block + 1) + 2);
        QMouseEvent move(QEvent::MouseMove, over, diff.viewport()->mapToGlobal(over), Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QApplication::sendEvent(diff.viewport(), &move);
        QToolButton *stage = nullptr;
        for (QToolButton *button : diff.viewport()->findChildren<QToolButton *>()) {
            if (button->text() == "Stage")
                stage = button;
        }
        if (!stage)
            QFAIL("no Stage button over the block");
        QVERIFY(stage->isVisible());
        stage->click();
        QCOMPARE(applied.size(), 1);
        QCOMPARE(applied.first().at(0).toInt(), block);
    }

    void blockButtonsHideWhenTheDiffScrolls()
    {
        const QByteArray patch = "@@ -1 +1 @@\n-a\n+b\n@@ -40 +40 @@\n-c\n+d\n@@ -80 +80 @@\n-e\n+f\n";
        DiffView diff(Theme::dark());
        const HunkActions actions(&diff);
        diff.resize(600, 120); // a few lines: the last block needs scrolling
        diff.show();
        QVERIFY(QTest::qWaitForWindowExposed(&diff));
        diff.setFiles({{"x.txt", "modified", FilePatch::parse(patch)}});
        const int block = diff.hunkStart(diff.fileStart(0) + 2);
        QVERIFY(block >= 0);
        const QPoint over(100, diff.lineTop(block + 1) + 2);
        QMouseEvent move(QEvent::MouseMove, over, diff.viewport()->mapToGlobal(over), Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QApplication::sendEvent(diff.viewport(), &move);
        const QList<QToolButton *> buttons = diff.viewport()->findChildren<QToolButton *>();
        const auto anyVisible = [&buttons] {
            return std::any_of(buttons.begin(), buttons.end(), [](const QToolButton *b) { return b->isVisible(); });
        };
        QVERIFY(anyVisible());

        // As the keyboard scrolls: no wheel, no mouse move.
        diff.goToChange(true);
        diff.goToChange(true);

        QTRY_VERIFY(!anyVisible());
    }

    void diffColorsTheCodeAsTheEditor()
    {
        const FilePatch patch = FilePatch::parse("@@ -1 +1 @@\n-// old comment\n+return value;\n");
        DiffView diff(Theme::dark());
        diff.setFiles({{"x.cpp", "modified", patch}});
        int removed = -1;
        int added = -1;
        for (int i = 0; i < diff.displayedLines().size(); ++i) {
            if (diff.displayedLines()[i].kind == DiffLineKind::Removed)
                removed = i;
            if (diff.displayedLines()[i].kind == DiffLineKind::Added)
                added = i;
        }
        const auto styleAt = [&diff](int line) {
            const long position = diff.SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, line);
            return int(diff.SendScintilla(QsciScintillaBase::SCI_GETSTYLEAT, position));
        };

        const int comment = styleAt(removed);
        const int keyword = styleAt(added);

        // Both sides colored, each with its own syntax style (not the plain one).
        QVERIFY(comment != 0);
        QVERIFY(keyword != 0);
        QVERIFY(comment != keyword);
    }

    void diffColorsEveryFileOfALanguage()
    {
        // One scratch editor colors every C++ file: the second one too.
        const FilePatch patch = FilePatch::parse("@@ -1 +1 @@\n-// old comment\n+return value;\n");
        DiffView diff(Theme::dark());

        diff.setFiles({{"x.cpp", "modified", patch}, {"y.cpp", "modified", patch}});

        const QList<DiffLine> &lines = diff.displayedLines();
        const int lastAdded = static_cast<int>(lines.size()) - 1;
        QCOMPARE(lines[lastAdded].kind, DiffLineKind::Added);
        const long position = diff.SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, lastAdded);
        QVERIFY(diff.SendScintilla(QsciScintillaBase::SCI_GETSTYLEAT, position) != 0);
    }

    void outsideARepository()
    {
        const QTemporaryDir project;
        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.setFolder(project.path());
        const QSignalSpy refreshed(&view, &ChangesView::refreshed);
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 1);
        const auto *list = view.findChild<QTreeWidget *>();
        QVERIFY(list->topLevelItem(0)->isHidden());
        QVERIFY(!commitButton(view)->isEnabled());
    }

    void stageAllThenCommit()
    {
        const QTemporaryDir project;
        write(project, "a.txt", "a\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "init"});
        write(project, "a.txt", "b\n");
        write(project, "new.txt", "n\n");

        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.show();
        view.setFolder(project.path());
        const QSignalSpy refreshed(&view, &ChangesView::refreshed);
        view.refresh(); // off the UI thread
        QTRY_COMPARE(refreshed.size(), 1);
        const auto *list = view.findChild<QTreeWidget *>();
        QCOMPARE(sectionTitles(list), QStringList({"Unstaged (2)", "Staged (0)"}));
        QCOMPARE(list->topLevelItem(0)->child(0)->text(0), QString("a.txt"));
        QCOMPARE(list->topLevelItem(0)->child(0)->data(0, FileChangeDelegate::kindRole).toInt(),
                 static_cast<int>(ChangeKind::Modified));
        QCOMPARE(list->topLevelItem(0)->child(1)->data(0, FileChangeDelegate::kindRole).toInt(),
                 static_cast<int>(ChangeKind::Added));

        view.stageAll();
        QTRY_COMPARE(sectionTitles(list), QStringList({"Unstaged (0)", "Staged (2)"}));

        // The last commit's message is offered for "amend", then removed.
        auto *message = view.findChild<QPlainTextEdit *>();
        auto *amend = view.findChild<QCheckBox *>();
        amend->setChecked(true);
        QCOMPARE(message->toPlainText(), QString("init"));
        amend->setChecked(false);
        QVERIFY(message->toPlainText().isEmpty());

        message->setPlainText("second");
        commitButton(view)->click();
        QTRY_COMPARE(sectionTitles(list), QStringList({"Unstaged (0)", "Staged (0)"}));
        QVERIFY(message->toPlainText().isEmpty());
        QCOMPARE(RepositoryInfo::read(project.path())->headMessage, QString("second\n"));
    }

    void snapshotsPopupListsAndRestores()
    {
        const QTemporaryDir project;
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        write(project, "a.txt", "a\n");
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "init"});
        write(project, "a.txt", "changed\n");
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        Snapshots::create(info, "Before discarding a.txt", {"a.txt"});
        write(project, "a.txt", "later\n");

        QWidget window;
        GitCommands commands;
        SnapshotsPopup popup(&commands, Theme::dark(), &window);
        popup.load(project.path());
        QCOMPARE(popup.list()->count(), 1);
        QCOMPARE(popup.detailsTitle()->text(), QString("Before discarding a.txt"));
        QCOMPARE(popup.files()->count(), 1);
        QCOMPARE(popup.files()->item(0)->text(), QString("a.txt"));

        const QSignalSpy succeeded(&commands, &GitCommands::succeeded);
        popup.restoreSelected();
        QTRY_COMPARE(succeeded.count(), 1);
        QFile file(project.filePath("a.txt"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("changed\n"));
    }

    void snapshotsPopupWithoutSnapshots()
    {
        const QTemporaryDir project;
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        QWidget window;
        GitCommands commands;
        SnapshotsPopup popup(&commands, Theme::dark(), &window);
        popup.load(project.path());
        QCOMPARE(popup.list()->count(), 0);
    }

    void snapshotDatesReadNaturally()
    {
        const QDateTime now(QDate(2026, 10, 16), QTime(15, 0)); // a Friday
        QCOMPARE(SnapshotsPopup::describeDate(now.addSecs(-20), now), QString("Just now"));
        QCOMPARE(SnapshotsPopup::describeDate(now.addSecs(-12LL * 60), now), QString("12 min ago"));
        QVERIFY(SnapshotsPopup::describeDate(now.addSecs(-3LL * 3600), now).startsWith("Today at "));
        QVERIFY(SnapshotsPopup::describeDate(now.addDays(-1), now).startsWith("Yesterday at "));
        QVERIFY(SnapshotsPopup::describeDate(now.addDays(-3), now).startsWith(QLocale().dayName(2)));
    }

    void newBranchPopupChecksTheName()
    {
        QWidget window;
        NewBranchPopup popup(Theme::dark(), &window);
        RepositoryInfo info;
        info.branch = "main";
        info.headId = "abc1234";
        popup.load(info, {"main", "feature/x"});
        QSignalSpy requested(&popup, &NewBranchPopup::createRequested);

        QVERIFY(!popup.createButton()->isEnabled()); // no name yet
        QTest::keyClicks(popup.nameField(), "fix login bug");
        QCOMPARE(popup.nameField()->text(), QString("fix-login-bug")); // as typed
        QVERIFY(popup.createButton()->isEnabled());

        popup.nameField()->setText("feature/x");
        QVERIFY(!popup.createButton()->isEnabled());
        QVERIFY(popup.message()->text().contains("already exists"));
        popup.nameField()->setText("bad..name");
        QVERIFY(!popup.createButton()->isEnabled());

        popup.nameField()->setText("feature/y");
        popup.switchToggle()->setChecked(false);
        popup.create();
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.first().at(0).toString(), QString("feature/y"));
        QCOMPARE(requested.first().at(1).toBool(), false);
        popup.switchToggle()->setChecked(true); // the saved choice back to its default
    }

    void branchesPopupFiltersAndSwitches()
    {
        QTemporaryDir project;
        write(project, "a.txt", "a\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "init"});
        Git::run(project.path(), {"branch", "feature/a"});
        Git::run(project.path(), {"branch", "feature/b"});

        QWidget window;
        GitCommands commands;
        GitActions actions(&commands, &window, [&] { return project.path(); });
        BranchesPopup popup(&actions, Theme::dark(), &window);
        popup.load(project.path());
        QStringList rows;
        for (int i = 0; i < popup.list()->count(); ++i)
            rows.append(popup.list()->item(i)->text());
        QCOMPARE(rows, QStringList({"Local", "feature/a", "feature/b", "main"}));
        QCOMPARE(popup.selected()->name, QString("feature/a")); // not the current branch

        QTest::keyClicks(popup.filterField(), "/B");
        QCOMPARE(popup.list()->count(), 2); // the heading, feature/b
        QCOMPARE(popup.selected()->name, QString("feature/b"));
        QTest::keyClick(popup.filterField(), Qt::Key_Return);
        QTRY_COMPARE(RepositoryInfo::read(project.path())->branch, QString("feature/b"));

        popup.load(project.path());
        popup.filterField()->setText("main");
        QVERIFY(popup.switchButton()->isEnabled());
        popup.filterField()->setText("feature/b");
        QVERIFY(popup.selected()->current);
        QVERIFY(!popup.switchButton()->isEnabled());
    }

    void stashesPopupShowsTheirChanges()
    {
        QTemporaryDir project;
        write(project, "a.txt", "a\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "init"});
        write(project, "a.txt", "changed\n");
        write(project, "new.txt", "new\n");
        Stashes::push(*RepositoryInfo::read(project.path()), "work in progress", true);

        QWidget window;
        GitCommands commands;
        GitActions actions(&commands, &window, [&] { return project.path(); });
        StashesPopup popup(&actions, Theme::dark(), &window);
        popup.load(project.path());
        QCOMPARE(popup.list()->count(), 1);
        QVERIFY(popup.list()->item(0)->text().startsWith("On main: work in progress"));

        const QList<DiffFile> files = StashesPopup::splitPatch(Stashes::patch(project.path(), 0));
        QCOMPARE(files.size(), 2);
        QCOMPARE(files[0].path, QString("a.txt"));
        QCOMPARE(files[0].status, QString("modified"));
        QCOMPARE(files[1].path, QString("new.txt"));
        QCOMPARE(files[1].status, QString("added"));
        QCOMPARE(files[1].patch.addedLines(), 1);
    }

    void operationBarDuringAMerge()
    {
        const QTemporaryDir project;
        write(project, "a.txt", "a\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "init"});
        Git::run(project.path(), {"switch", "-qc", "other"});
        write(project, "a.txt", "other\n");
        Git::run(project.path(), {"commit", "-qam", "other"});
        Git::run(project.path(), {"switch", "-q", "main"});
        write(project, "a.txt", "main\n");
        Git::run(project.path(), {"commit", "-qam", "main"});
        QVERIFY_THROWS_EXCEPTION(GitError, Git::run(project.path(), {"merge", "other"}));

        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.show();
        view.setFolder(project.path());
        QVERIFY(!view.operationBar()->isVisible());
        const QSignalSpy refreshed(&view, &ChangesView::refreshed);
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 1);
        QVERIFY(view.operationBar()->isVisible());
        QVERIFY2(view.operationText()->text().contains("1 conflict"), qPrintable(view.operationText()->text()));
        QVERIFY(!view.continueButton()->isEnabled());

        write(project, "a.txt", "resolved\n");
        Git::run(project.path(), {"add", "a.txt"});
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 2);
        QVERIFY(view.continueButton()->isEnabled());
        const QSignalSpy continued(&view, &ChangesView::continueRequested);
        view.continueButton()->click();
        QCOMPARE(continued.size(), 1);

        Git::run(project.path(), {"commit", "--no-edit", "-q"});
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 3);
        QVERIFY(!view.operationBar()->isVisible());
    }

    void readsTheChangesOfTheNextCommit()
    {
        const QTemporaryDir project;
        write(project, "a.txt", "a\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "Add a"});

        // Amending the first commit: against the empty tree.
        const RepositoryInfo info = *RepositoryInfo::read(project.path());
        CommitMessage::Changes changes = CommitMessage::read(info, true, false, {});
        QCOMPARE(changes.recentSubjects, QStringList{"Add a"});
        QVERIFY(changes.diff.contains("+a"));

        write(project, "a.txt", "b\n");
        write(project, "b.txt", "staged\n");
        write(project, "new.txt", "n\n");
        Git::run(project.path(), {"add", "b.txt"});

        changes = CommitMessage::read(info, false, false, {});
        QVERIFY(changes.diff.contains("+staged"));
        QVERIFY(!changes.diff.contains("+b"));
        QVERIFY(changes.stat.contains("b.txt"));
        QVERIFY(changes.untracked.isEmpty());

        changes = CommitMessage::read(info, false, true, {});
        QVERIFY(changes.diff.contains("+staged"));
        QVERIFY(changes.diff.contains("+b"));
        QCOMPARE(changes.untracked, QStringList{"new.txt"});
    }

#ifndef Q_OS_WIN
    // With a stand-in for the `claude` command, found in the PATH.
    void generatesTheCommitMessage()
    {
        const QTemporaryDir bin;
        const QString claude = bin.filePath("claude");
        QFile script(claude);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\n"
                     "cat > \"$0.input\"\n"
                     "echo \"$@\" > \"$0.arguments\"\n"
                     "printf '%s' '{\"type\":\"result\",\"is_error\":false,\"result\":\"Change a\\n\\nBecause.\"}'\n");
        script.close();
        script.setPermissions(script.permissions() | QFileDevice::ExeOwner);
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", bin.path().toUtf8() + ':' + path);

        const QTemporaryDir project;
        write(project, "a.txt", "a\n");
        Git::run(project.path(), {"init", "-q", "-b", "main"});
        Git::run(project.path(), {"add", "-A"});
        Git::run(project.path(), {"commit", "-qm", "Add a"});
        write(project, "a.txt", "b\n");

        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.setFolder(project.path());
        const QSignalSpy refreshed(&view, &ChangesView::refreshed);
        view.refresh();
        QTRY_COMPARE(refreshed.size(), 1);

        auto *message = view.findChild<QPlainTextEdit *>();
        message->setPlainText("mine");
        view.generateCommitMessage(); // nothing staged: every change
        QTRY_COMPARE(message->toPlainText(), QString("Change a\n\nBecause."));
        qputenv("PATH", path);

        QFile input(claude + ".input");
        QVERIFY(input.open(QIODevice::ReadOnly));
        const QByteArray sent = input.readAll();
        QVERIFY(sent.contains("Recent commits:\nAdd a\n"));
        QVERIFY(sent.contains("+b"));
        QFile arguments(claude + ".arguments");
        QVERIFY(arguments.open(QIODevice::ReadOnly));
        QVERIFY(arguments.readAll().startsWith("--print --model haiku"));

        message->undo(); // what was written before comes back
        QCOMPARE(message->toPlainText(), QString("mine"));
    }
#endif

private:
    QTemporaryFile config_;
};

QTEST_MAIN(TestChanges)
#include "tst_changes.moc"
