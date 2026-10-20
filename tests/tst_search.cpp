// SPDX-License-Identifier: GPL-3.0-only

#include "git/GitProcess.h"
#include "search/FindInFiles.h"
#include "search/FindInFilesPopup.h"

#include <QLineEdit>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

namespace {

void write(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

} // namespace

class TestSearch : public QObject {
    Q_OBJECT

private slots:

    void queryOptions()
    {
        FindQuery query;
        query.text = "a.b";
        QVERIFY(query.compile()->match("xA.By").hasMatch());
        QVERIFY(!query.compile()->match("aXb").hasMatch()); // literal "."
        query.matchCase = true;
        QVERIFY(!query.compile()->match("A.B").hasMatch());
        query.text = "foo";
        query.wholeWords = true;
        QVERIFY(!query.compile()->match("foobar").hasMatch());
        QVERIFY(query.compile()->match("a foo b").hasMatch());
        query.regularExpression = true;
        query.text = "fo(";
        QString error;
        QVERIFY(!query.compile(&error));
        QVERIFY(!error.isEmpty());
        query.text.clear();
        QVERIFY(!query.compile());
    }

    void fileMask()
    {
        FindQuery query;
        QVERIFY(query.acceptsFile("anything.bin"));
        query.fileMask = "*.cpp, *.H";
        QVERIFY(query.acceptsFile("a.cpp"));
        QVERIFY(query.acceptsFile("a.h"));
        QVERIFY(!query.acceptsFile("a.js"));
        query.fileMask = "!*.min.js";
        QVERIFY(query.acceptsFile("a.js"));
        QVERIFY(!query.acceptsFile("a.min.js"));
    }

    void matchesHaveLinesAndColumns()
    {
        FindQuery query;
        query.text = "é";
        const QList<FindMatch> matches = FindInFiles::search("abc\r\n  été\nno\n", *query.compile(), "/f", 100);
        QCOMPARE(matches.size(), 2);
        QCOMPARE(matches[0].line, 1);
        QCOMPARE(matches[0].column, 2);
        QCOMPARE(matches[0].length, 1);
        QCOMPARE(matches[0].lineText, QString("  été"));
        QCOMPARE(matches[1].column, 4);
        QCOMPARE(FindInFiles::search("ééé", *query.compile(), "/f", 2).size(), 2);
    }

    void searchSkipsIgnoredAndBinaryFilesAndReadsBuffers()
    {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("git unavailable");
        const QTemporaryDir dir;
        write(dir.filePath("src/a.cpp"), "int needle = 1;\n");
        write(dir.filePath("src/b.h"), "// needle needle\n");
        write(dir.filePath("build/out.cpp"), "needle\n");
        write(dir.filePath(".github/ci.yml"), "needle: true\n");
        write(dir.filePath("data.bin"), QByteArray("needle\0", 7));
        write(dir.filePath("open.txt"), "nothing on disk\n");
        write(dir.filePath(".gitignore"), "build/\n");
        Git::run(dir.path(), {"init", "-q"});

        FindInFilesSearch search;
        QSignalSpy finished(&search, &FindInFilesSearch::finished);
        FindQuery query;
        query.text = "needle";
        search.start(dir.path(), query, {{dir.filePath("open.txt"), "unsaved needle\n"}});
        QVERIFY(finished.wait());
        const auto matches = finished.first().at(0).value<QList<FindMatch>>();
        QSet<QString> files;
        for (const FindMatch &match : matches)
            files.insert(QDir(dir.path()).relativeFilePath(match.filePath));
        QCOMPARE(files, (QSet<QString>{"src/a.cpp", "src/b.h", ".github/ci.yml", "open.txt"}));
        QCOMPARE(matches.size(), 5);

        query.fileMask = "*.h";
        search.start(dir.path(), query);
        QVERIFY(finished.wait());
        QCOMPARE(finished.last().at(0).value<QList<FindMatch>>().size(), 2);
    }

    void popupListsAndOpensMatches()
    {
        const QTemporaryDir dir;
        write(dir.filePath("a.txt"), "first\n    second needle here\n");
        QWidget window;
        window.resize(1000, 800);
        window.show(); // the popup is a panel inside it
        FindInFilesPopup popup(Theme::dark(), &window);
        popup.setFolder(dir.path());
        QSignalSpy chosen(&popup, &FindInFilesPopup::matchChosen);
        popup.popup("needle");
        QTRY_COMPARE(popup.results()->topLevelItemCount(), 1);
        // Leading spaces are not shown.
        QCOMPARE(popup.results()->topLevelItem(0)->text(0), QString("second needle here"));
        QCOMPARE(popup.results()->topLevelItem(0)->text(1), QString("a.txt 2"));
        QTest::keyClick(popup.searchField(), Qt::Key_Return);
        QTRY_COMPARE(chosen.size(), 1);
        QCOMPARE(chosen.first().at(1).toInt(), 1);
        QCOMPARE(chosen.first().at(2).toInt(), 11);
        QCOMPARE(chosen.first().at(3).toInt(), 6);
    }
};

QTEST_MAIN(TestSearch)
#include "tst_search.moc"
