// SPDX-License-Identifier: GPL-3.0-only

#include "changes/GitActions.h"
#include "git/ConflictVersions.h"
#include "git/GitCommands.h"
#include "git/GitProcess.h"
#include "merge/MergeCompletedPopup.h"
#include "merge/MergePane.h"
#include "merge/MergeReadyPanel.h"
#include "merge/MergeView.h"
#include "merge/MergeWindow.h"
#include "ui/AppFonts.h"
#include "ui/ThemeStyle.h"
#include "workspace/ChoicePopup.h"
#include "workspace/EntryPopups.h"

#include <QFile>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

namespace {

struct Repository {
    QTemporaryDir dir;

    Repository()
    {
        git({"init", "-q", "-b", "main"});
        git({"config", "core.autocrlf", "false"});
        write("base\n");
        commit("base");
        git({"switch", "-qc", "feature"});
        write("incoming\n");
        commit("incoming");
        git({"switch", "-q", "main"});
    }

    QByteArray git(const QStringList &arguments) const { return Git::run(dir.path(), arguments); }

    RepositoryInfo info() const { return *RepositoryInfo::read(dir.path()); }

    void write(const QByteArray &text) const
    {
        QFile file(dir.filePath("a.txt"));
        if (!file.open(QIODevice::WriteOnly) || file.write(text) != text.size())
            qFatal("Cannot write fixture");
    }

    void commit(const QString &message) const
    {
        git({"add", "a.txt"});
        git({"commit", "-qm", message});
    }

    void conflict() const
    {
        write("current\n");
        commit("current");
        QVERIFY_THROWS_EXCEPTION(GitError, git({"merge", "--no-edit", "feature"}));
    }
};

QPushButton *button(QWidget &widget, const QString &text)
{
    for (QPushButton *candidate : widget.findChildren<QPushButton *>()) {
        if (candidate->text() == text)
            return candidate;
    }
    return nullptr;
}

void takeIncoming(QWidget &widget)
{
    const auto *header = widget.findChild<QWidget *>("mergeIncomingHeader");
    QVERIFY2(header, "No incoming version button");
    header->findChild<QPushButton *>()->click();
}

void capture(QWidget &widget, const QString &name)
{
    const QString folder = qEnvironmentVariable("KDK_MERGE_CAPTURE_DIR");
    if (folder.isEmpty())
        return;
    QDir().mkpath(folder);
    // Child widgets added by an asynchronous read are shown on the next turn.
    QTest::qWait(50);
    widget.grab().save(QDir(folder).filePath(name + ".png"));
}

} // namespace

class TestMergeWindow : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        QVERIFY(config_.open());
        qputenv("GIT_CONFIG_GLOBAL", config_.fileName().toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_AUTHOR_NAME", "Test");
        qputenv("GIT_AUTHOR_EMAIL", "test@example.com");
        qputenv("GIT_COMMITTER_NAME", "Test");
        qputenv("GIT_COMMITTER_EMAIL", "test@example.com");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
        QCoreApplication::setOrganizationName("KodamikoTests");
        QCoreApplication::setApplicationName("MergeWindow");
        AppFonts::load();
        ThemeStyle::apply(Theme::kodamiko());
    }

    void successClosesTheWindowBeforeShowingTheConfirmation()
    {
        Repository repository;
        repository.conflict();
        GitCommands commands;
        QWidget editor;
        editor.resize(1400, 900);
        editor.show();
        MergeCompletedPopup popup(Theme::kodamiko(), &editor);
        MergeWindow window(repository.dir.path(), Theme::kodamiko(), &commands, &editor);
        QSignalSpy finished(&window, &MergeWindow::finished);
        bool visibleAtCompletion = true;
        connect(&window, &MergeWindow::finished, &popup, [&](const Integration::Completion &completion) {
            visibleAtCompletion = window.isVisible();
            popup.popup(completion);
        });
        window.open();
        window.resize(1400, 850);
        QVERIFY(window.isWindow());
        QTRY_COMPARE(window.findChild<QListWidget *>()->count(), 1);
        capture(window, "01-resolution");
        QVERIFY(!button(window, "Save Resolution")->isEnabled());
        takeIncoming(window);
        button(window, "Save Resolution")->click();
        QTRY_VERIFY(window.findChild<MergeReadyPanel *>()->isVisible());
        QVERIFY(window.isVisible());
        QCOMPARE(finished.count(), 0);
        auto *message = window.findChild<QPlainTextEdit *>("mergeCommitMessage");
        QVERIFY(!message->toPlainText().contains("# Conflicts"));
        message->setPlainText("Integrate the reviewed pricing changes");
        capture(window, "02-finalisation");
        button(window, "Review Files")->click();
        QVERIFY(!window.findChild<MergeReadyPanel *>()->isVisible());
        button(window, "Back to Summary")->click();
        QCOMPARE(message->toPlainText(), QString("Integrate the reviewed pricing changes"));

        button(window, "Finish Merge")->click();
        QVERIFY(window.isVisible());
        QVERIFY(commands.isBusy());
        QVERIFY(!window.close());
        QVERIFY(!popup.isVisible());
        QTRY_COMPARE(finished.count(), 1);
        QTRY_VERIFY(!commands.isBusy());
        QTest::qWait(50);
        QCOMPARE(finished.count(), 1);

        QVERIFY(!visibleAtCompletion);
        QVERIFY(!window.isVisible());
        QVERIFY(popup.isVisible());
        QVERIFY(repository.info().operation.isEmpty());
        QCOMPARE(repository.info().headMessage.trimmed(), QString("Integrate the reviewed pricing changes"));
        QCOMPARE(repository.git({"rev-list", "--parents", "-n1", "HEAD"}).trimmed().split(' ').size(), 3);
        const auto completion = qvariant_cast<Integration::Completion>(finished.at(0).at(0));
        QCOMPARE(completion.sides.current, QString("main"));
        QCOMPARE(completion.sides.incoming, QString("feature"));
        QCOMPARE(completion.commitId, repository.info().headId);
        capture(editor, "03-confirmation");
        QSignalSpy requested(&popup, &MergeCompletedPopup::commitRequested);
        button(popup, "View Commit")->click();
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.at(0).at(0).toString(), completion.commitId);
        QVERIFY(!popup.isVisible());
    }

    void hidingTheWindowPreservesAnUnsavedResolution()
    {
        Repository repository;
        repository.conflict();
        GitCommands commands;
        QWidget editor;
        MergeWindow window(repository.dir.path(), Theme::kodamiko(), &commands, &editor);
        window.open();
        QTRY_COMPARE(window.findChild<QListWidget *>()->count(), 1);
        takeIncoming(window);
        QVERIFY(button(window, "Save Resolution")->isEnabled());
        QVERIFY(window.close());
        QVERIFY(!window.isVisible());
        window.open();
        QTest::qWait(50);
        QVERIFY(button(window, "Save Resolution")->isEnabled());
        for (MergePane *pane : window.findChildren<MergePane *>()) {
            if (!pane->isReadOnly())
                QCOMPARE(pane->bytes(), QByteArray("incoming\n"));
        }
        QCOMPARE(ConflictVersions::read(repository.info()).size(), 1);
    }

    void failedFinalizationKeepsTheWindowAndMessageForRetry()
    {
        Repository repository;
        repository.conflict();
        GitCommands commands;
        QWidget editor;
        MergeWindow window(repository.dir.path(), Theme::kodamiko(), &commands, &editor);
        QSignalSpy finished(&window, &MergeWindow::finished);
        QSignalSpy failed(&commands, &GitCommands::failed);
        window.open();
        QTRY_COMPARE(window.findChild<QListWidget *>()->count(), 1);
        takeIncoming(window);
        button(window, "Save Resolution")->click();
        QTRY_VERIFY(window.findChild<MergeReadyPanel *>()->isVisible());
        auto *message = window.findChild<QPlainTextEdit *>("mergeCommitMessage");
        message->clear();
        QVERIFY(!button(window, "Finish Merge")->isEnabled());
        message->setPlainText("Keep this message on failure");
        repository.git({"config", "commit.gpgSign", "true"});
        repository.git({"config", "gpg.program", "kodamiko-nonexistent-signing-program"});

        button(window, "Finish Merge")->click();
        QTRY_COMPARE(failed.count(), 1);
        QTRY_VERIFY(button(window, "Finish Merge")->isEnabled());

        QCOMPARE(finished.count(), 0);
        QVERIFY(window.isVisible());
        QCOMPARE(repository.info().operation, QString("merge"));
        QCOMPARE(message->toPlainText(), QString("Keep this message on failure"));
        repository.git({"config", "commit.gpgSign", "false"});
        button(window, "Finish Merge")->click();
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!window.isVisible());
    }

    void abortHasItsOwnConfirmationAndNeverReportsSuccess()
    {
        Repository repository;
        repository.conflict();
        const QString previousHead = repository.info().headId;
        GitCommands commands;
        QWidget editor;
        MergeWindow window(repository.dir.path(), Theme::kodamiko(), &commands, &editor);
        QSignalSpy finished(&window, &MergeWindow::finished);
        QSignalSpy aborted(&window, &MergeWindow::aborted);
        window.open();
        QTRY_COMPARE(window.findChild<QListWidget *>()->count(), 1);
        button(window, "Abort merge")->click();
        auto *confirm = window.findChild<ConfirmPopup *>();
        QVERIFY(confirm);
        QVERIFY(confirm->isVisible());
        confirm->cancelButton()->click();
        QVERIFY(window.isVisible());
        QCOMPARE(repository.info().operation, QString("merge"));
        button(window, "Abort merge")->click();
        const auto confirmations = window.findChildren<ConfirmPopup *>();
        confirmations.last()->actionButton()->click();
        QTRY_COMPARE(aborted.count(), 1);
        QCOMPARE(finished.count(), 0);
        QVERIFY(!window.isVisible());
        QCOMPARE(repository.info().headId, previousHead);
        QVERIFY(repository.info().operation.isEmpty());
    }

    void anIntermediateRebaseConflictDoesNotCloseTheWindow()
    {
        Repository repository;
        repository.git({"switch", "-q", "feature"});
        repository.write("second incoming\n");
        repository.commit("second incoming");
        repository.git({"switch", "-q", "main"});
        repository.write("current\n");
        repository.commit("current");
        repository.git({"switch", "-q", "feature"});
        QVERIFY_THROWS_EXCEPTION(GitError, repository.git({"rebase", "main"}));
        GitCommands commands;
        QWidget editor;
        MergeWindow window(repository.dir.path(), Theme::kodamiko(), &commands, &editor);
        QSignalSpy finished(&window, &MergeWindow::finished);
        window.open();
        QTRY_COMPARE(window.findChild<QListWidget *>()->count(), 1);
        // An independent resolution makes the next replay conflict too.
        Integration::writeResolution(repository.info(), "a.txt", "combined first\n");
        window.reload();
        QTRY_VERIFY(window.findChild<MergeReadyPanel *>()->isVisible());
        button(window, "Continue rebase")->click();
        // The stop is reported over the files, not as a failure.
        QTRY_VERIFY(window.findChild<ChoicePopup *>());
        QTRY_COMPARE(window.findChild<QListWidget *>()->count(), 1);
        QCOMPARE(finished.count(), 0);
        QVERIFY(window.isVisible());
        QCOMPARE(repository.info().operation, QString("rebase"));
        takeIncoming(window);
        button(window, "Save Resolution")->click();
        QTRY_VERIFY(window.findChild<MergeReadyPanel *>()->isVisible());
        button(window, "Continue rebase")->click();
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(repository.info().operation.isEmpty());
        QVERIFY(!window.isVisible());
    }

    void aMergeWithoutConflictsReportsSuccessOnlyOnce()
    {
        Repository repository;
        GitCommands commands;
        QWidget editor;
        GitActions actions(&commands, &editor, [&repository] { return repository.dir.path(); });
        QSignalSpy completed(&actions, &GitActions::integrationFinished);
        actions.merge("feature");
        QTRY_VERIFY(!commands.isBusy());
        QCOMPARE(completed.count(), 1);
        QCOMPARE(qvariant_cast<Integration::Completion>(completed.at(0).at(0)).commitId, repository.info().headId);
        actions.merge("feature");
        QTRY_VERIFY(!commands.isBusy());
        QCOMPARE(completed.count(), 1);
    }

    void aMergeStoppedOnConflictsIsAStopNotAFailure()
    {
        Repository repository;
        repository.write("current\n");
        repository.commit("current");
        GitCommands commands;
        QWidget editor;
        GitActions actions(&commands, &editor, [&repository] { return repository.dir.path(); });
        QSignalSpy stopped(&actions, &GitActions::stoppedOnConflicts);
        QSignalSpy failed(&commands, &GitCommands::failed);
        actions.merge("feature");
        QTRY_VERIFY(!commands.isBusy());
        QCOMPARE(stopped.count(), 1);
        QVERIFY(stopped.at(0).at(1).toString().contains("CONFLICT"));
        QCOMPARE(failed.count(), 0);
    }

    void aFailureWithoutConflictsIsNotAStop()
    {
        Repository repository;
        GitCommands commands;
        QWidget editor;
        GitActions actions(&commands, &editor, [&repository] { return repository.dir.path(); });
        QSignalSpy stopped(&actions, &GitActions::stoppedOnConflicts);
        QSignalSpy failed(&commands, &GitCommands::failed);
        actions.merge("no-such-branch");
        QTRY_VERIFY(!commands.isBusy());
        QCOMPARE(stopped.count(), 0);
        QCOMPARE(failed.count(), 1);
    }

    void recognizesOnlyARealMergeCompletedOutsideTheWindow_data()
    {
        QTest::addColumn<bool>("complete");
        QTest::addColumn<bool>("unrelatedCommit");
        QTest::newRow("external merge commit") << true << false;
        QTest::newRow("external abort") << false << false;
        QTest::newRow("abort followed by another commit") << false << true;
    }

    void recognizesOnlyARealMergeCompletedOutsideTheWindow()
    {
        QFETCH(bool, complete);
        QFETCH(bool, unrelatedCommit);
        Repository repository;
        repository.conflict();
        GitCommands commands;
        QWidget editor;
        MergeWindow window(repository.dir.path(), Theme::kodamiko(), &commands, &editor);
        QSignalSpy finished(&window, &MergeWindow::finished);
        window.open();
        QTRY_COMPARE(window.findChild<QListWidget *>()->count(), 1);
        if (complete) {
            Integration::writeResolution(repository.info(), "a.txt", "combined\n");
            Integration::continueOperation(repository.info());
        } else {
            Integration::abortOperation(repository.info());
        }
        if (unrelatedCommit) {
            repository.write("unrelated\n");
            repository.commit("another change");
        }
        window.reload();
        if (complete) {
            QTRY_COMPARE(finished.count(), 1);
            QVERIFY(!window.isVisible());
        } else {
            QTRY_VERIFY(!window.isVisible());
            QCOMPARE(finished.count(), 0);
        }
    }

private:
    QTemporaryFile config_;
    QTemporaryDir settings_;
};

QTEST_MAIN(TestMergeWindow)
#include "tst_mergewindow.moc"
