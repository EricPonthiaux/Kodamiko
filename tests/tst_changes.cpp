#include "changes/ChangesView.h"
#include "changes/DiffView.h"
#include "changes/FileChangeDelegate.h"
#include "changes/NewBranchPopup.h"
#include "changes/SnapshotsPopup.h"
#include "git/GitCommands.h"
#include "git/GitProcess.h"
#include "git/RepositoryStatus.h"
#include "git/Snapshots.h"
#include "ui/PopupWidgets.h"

#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
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

QStringList sectionTitles(const QTreeWidget *list)
{
    QStringList titles;
    for (int i = 0; i < list->topLevelItemCount(); ++i)
        titles << QStringLiteral("%1 (%2)").arg(list->topLevelItem(i)->text(0)).arg(
                      list->topLevelItem(i)->data(0, FileChangeDelegate::SectionCountRole).toInt());
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
        const QList<DiffView::Line> lines = DiffView::layout({{"x.cpp", "modified", patch}});
        QStringList texts;
        for (const DiffView::Line &line : lines)
            texts.append(line.text);
        QCOMPARE(texts, QStringList({"", "x.cpp   modified", "⋯ lines 10–12  ·  void run()", "keep", "old", "new", "end",
                                     "⋯ line 40", "gone", "No line break at the end of the file"}));
        QCOMPARE(lines[0].kind, DiffView::LineKind::Spacer);
        QCOMPARE(lines[1].kind, DiffView::LineKind::FileHeader);
        QCOMPARE(lines[1].emphasis, 5); // "x.cpp"
        QCOMPARE(lines[4].kind, DiffView::LineKind::Removed);
        QCOMPARE(lines[4].oldNumber, 11);
        QCOMPARE(lines[4].newNumber, 0);
        QCOMPARE(lines[5].kind, DiffView::LineKind::Added);
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
        view.setFiles({{"src/x.cpp", "modified", patch}, {"y.bin", "added", FilePatch::parse("Binary files differ\n")}});
        QCOMPARE(view.lines()[1].text, QString("x.cpp   src  ·  modified"));
        QCOMPARE(view.lines().size(), lines.size() + 3);
        QCOMPARE(view.fileStart(1), static_cast<int>(lines.size()) + 1);
        QCOMPARE(view.locate(5).line, 2);
        QVERIFY(view.text().contains("Binary file"));
        QVERIFY(!view.text().contains("@@"));
    }

    void outsideARepository()
    {
        QTemporaryDir project;
        GitCommands commands;
        ChangesView view(Theme::dark(), &commands);
        view.setFolder(project.path());
        view.refresh();
        const auto *list = view.findChild<QTreeWidget *>();
        QVERIFY(list->topLevelItem(0)->isHidden());
        QVERIFY(!view.findChild<QPushButton *>()->isEnabled());
    }

    void stageAllThenCommit()
    {
        QTemporaryDir project;
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
        view.refresh();
        const auto *list = view.findChild<QTreeWidget *>();
        QCOMPARE(sectionTitles(list), QStringList({"Staged (0)", "Unstaged (2)"}));
        QCOMPARE(list->topLevelItem(1)->child(0)->text(0), QString("a.txt"));
        QCOMPARE(list->topLevelItem(1)->child(0)->data(0, FileChangeDelegate::KindRole).toInt(),
                 static_cast<int>(ChangeKind::Modified));
        QCOMPARE(list->topLevelItem(1)->child(1)->data(0, FileChangeDelegate::KindRole).toInt(),
                 static_cast<int>(ChangeKind::Added));

        view.stageAll();
        QTRY_COMPARE(sectionTitles(list), QStringList({"Staged (2)", "Unstaged (0)"}));

        // The last commit's message is offered for "amend", then removed.
        auto *message = view.findChild<QPlainTextEdit *>();
        auto *amend = view.findChild<QCheckBox *>();
        amend->setChecked(true);
        QCOMPARE(message->toPlainText(), QString("init"));
        amend->setChecked(false);
        QVERIFY(message->toPlainText().isEmpty());

        message->setPlainText("second");
        view.findChild<QPushButton *>()->click();
        QTRY_COMPARE(sectionTitles(list), QStringList({"Staged (0)", "Unstaged (0)"}));
        QVERIFY(message->toPlainText().isEmpty());
        QCOMPARE(RepositoryInfo::read(project.path())->headMessage, QString("second\n"));
    }

    void snapshotsPopupListsAndRestores()
    {
        QTemporaryDir project;
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

        QSignalSpy succeeded(&commands, &GitCommands::succeeded);
        popup.restoreSelected();
        QTRY_COMPARE(succeeded.count(), 1);
        QFile file(project.filePath("a.txt"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("changed\n"));
    }

    void snapshotsPopupWithoutSnapshots()
    {
        QTemporaryDir project;
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
        QCOMPARE(SnapshotsPopup::describeDate(now.addSecs(-12 * 60), now), QString("12 min ago"));
        QVERIFY(SnapshotsPopup::describeDate(now.addSecs(-3 * 3600), now).startsWith("Today at "));
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

private:
    QTemporaryFile config_;
};

QTEST_MAIN(TestChanges)
#include "tst_changes.moc"
