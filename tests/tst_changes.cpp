#include "changes/ChangesView.h"
#include "git/GitCommands.h"
#include "git/GitProcess.h"
#include "git/RepositoryStatus.h"

#include <QCheckBox>
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
        titles << list->topLevelItem(i)->text(0);
    return titles;
}

} // namespace

class TestChanges : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("git indisponible");
        QVERIFY(config_.open());
        qputenv("GIT_CONFIG_GLOBAL", config_.fileName().toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_AUTHOR_NAME", "t");
        qputenv("GIT_AUTHOR_EMAIL", "t@t");
        qputenv("GIT_COMMITTER_NAME", "t");
        qputenv("GIT_COMMITTER_EMAIL", "t@t");
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
        QCOMPARE(sectionTitles(list), QStringList({"Indexés (0)", "Non indexés (2)"}));
        QCOMPARE(list->topLevelItem(1)->child(0)->text(0), QString("M  a.txt"));
        QCOMPARE(list->topLevelItem(1)->child(1)->text(0), QString("A  new.txt"));

        view.stageAll();
        QTRY_COMPARE(sectionTitles(list), QStringList({"Indexés (2)", "Non indexés (0)"}));

        // Le message du dernier commit est proposé pour « amend », puis retiré.
        auto *message = view.findChild<QPlainTextEdit *>();
        auto *amend = view.findChild<QCheckBox *>();
        amend->setChecked(true);
        QCOMPARE(message->toPlainText(), QString("init"));
        amend->setChecked(false);
        QVERIFY(message->toPlainText().isEmpty());

        message->setPlainText("second");
        view.findChild<QPushButton *>()->click();
        QTRY_COMPARE(sectionTitles(list), QStringList({"Indexés (0)", "Non indexés (0)"}));
        QVERIFY(message->toPlainText().isEmpty());
        QCOMPARE(RepositoryInfo::read(project.path())->headMessage, QString("second\n"));
    }

private:
    QTemporaryFile config_;
};

QTEST_MAIN(TestChanges)
#include "tst_changes.moc"
