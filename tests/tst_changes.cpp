#include "changes/WorkingTreeDiff.h"

#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class TestChanges : public QObject {
    Q_OBJECT

private:
    static void write(const QTemporaryDir &dir, const QString &path, const QByteArray &content)
    {
        QDir().mkpath(QFileInfo(dir.filePath(path)).absolutePath());
        QFile file(dir.filePath(path));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    }

    // Lance git dans `dir` ; le test est ignoré si git n'est pas installé.
    static bool git(const QTemporaryDir &dir, const QStringList &arguments)
    {
        QProcess process;
        process.setWorkingDirectory(dir.path());
        process.start(QStringLiteral("git"),
                      QStringList{"-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false"}
                          + arguments);
        return process.waitForFinished() && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    }

private slots:
    void workingTreeDiffOutsideARepository()
    {
        QTemporaryDir project;
        write(project, "a.txt", "a\n");
        QList<WorkingTreeChange> changes;
        QVERIFY(!WorkingTreeDiff::compute(project.path(), &changes));
        QVERIFY(changes.isEmpty());
    }

    void workingTreeDiffComparesWithLastCommit()
    {
        QTemporaryDir project;
        write(project, "keep.txt", "same\n");
        write(project, "edit.cpp", "int a;\n");
        write(project, "gone.txt", "bye\n");
        if (!git(project, {"init", "-q"}) || !git(project, {"add", "."}) || !git(project, {"commit", "-qm", "init"}))
            QSKIP("git indisponible");

        write(project, "edit.cpp", "int b;\n");
        QFile::remove(project.filePath("gone.txt"));
        write(project, "src/new.js", "let x;\n");

        QList<WorkingTreeChange> changes;
        QVERIFY(WorkingTreeDiff::compute(project.path(), &changes));
        QList<FileChange> files;
        for (const WorkingTreeChange &change : changes)
            files.append(change.change);
        QCOMPARE(files.size(), 3);
        QVERIFY(files.contains(FileChange{ChangeKind::Modified, "edit.cpp", {}}));
        QVERIFY(files.contains(FileChange{ChangeKind::Deleted, "gone.txt", {}}));
        QVERIFY(files.contains(FileChange{ChangeKind::Added, "src/new.js", {}}));
        for (const WorkingTreeChange &change : changes) {
            if (change.change.path == "edit.cpp")
                QVERIFY(change.patch.contains("-int a;") && change.patch.contains("+int b;"));
            if (change.change.path == "src/new.js")
                QVERIFY(change.patch.contains("+let x;"));
        }
    }

    void workingTreeDiffOfASubfolderUsesRelativePaths()
    {
        QTemporaryDir project;
        write(project, "root.txt", "r\n");
        write(project, "sub/file.txt", "f\n");
        if (!git(project, {"init", "-q"}) || !git(project, {"add", "."}) || !git(project, {"commit", "-qm", "init"}))
            QSKIP("git indisponible");

        write(project, "root.txt", "r2\n");
        write(project, "sub/file.txt", "f2\n");

        QList<WorkingTreeChange> changes;
        QVERIFY(WorkingTreeDiff::compute(project.filePath("sub"), &changes));
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes.first().change, (FileChange{ChangeKind::Modified, "file.txt", {}}));
    }
};

QTEST_GUILESS_MAIN(TestChanges)
#include "tst_changes.moc"
