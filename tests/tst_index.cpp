// SPDX-License-Identifier: GPL-3.0-only

#include "index/OccurrenceIndex.h"
#include "index/OccurrenceScanner.h"
#include "index/OccurrenceStore.h"
#include "index/PostingCodec.h"

#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using Role = OccurrenceRole;

namespace {

void write(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

// The role of each occurrence of `word`, in text order.
QList<Role> rolesOf(const QList<Occurrence> &occurrences, const QByteArray &word)
{
    QList<Role> roles;
    for (const Occurrence &occurrence : occurrences) {
        if (occurrence.word == word)
            roles.append(occurrence.role);
    }
    return roles;
}

QSet<QString> setOf(const QStringList &paths)
{
    return {paths.cbegin(), paths.cend()};
}

QList<OccurrenceStore::Location> locationsOf(const OccurrenceStore &store, const QByteArray &word)
{
    QList<OccurrenceStore::Location> locations;
    store.forEachOccurrence(word, [&locations](const OccurrenceStore::Location &location) {
        locations.append(location);
        return true;
    });
    return locations;
}

} // namespace

class TestIndex : public QObject {
    Q_OBJECT

private slots:

    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void readsTheRolesOfCppIdentifiers()
    {
        OccurrenceScanner scanner;
        const QByteArray text = "#include \"total.h\"\n"
                                "// total of each count\n"
                                "int total(int count) {\n"
                                "  int sum = 0;\n"
                                "  sum += count;\n"
                                "  log(\"total sum\");\n"
                                "  return total(sum);\n"
                                "}\n";

        const QList<Occurrence> occurrences = scanner.scan("cpp", text);

        QCOMPARE(rolesOf(occurrences, "total"),
                 (QList<Role>{Role::Import, Role::Comment, Role::Definition, Role::String, Role::Call}));
        QCOMPARE(rolesOf(occurrences, "sum"),
                 (QList<Role>{Role::Definition, Role::Write, Role::String, Role::Reference}));
        QCOMPARE(rolesOf(occurrences, "count"), (QList<Role>{Role::Comment, Role::Definition, Role::Reference}));
        const auto definition = std::ranges::find_if(
            occurrences, [](const Occurrence &o) { return o.word == "total" && o.role == Role::Definition; });
        QCOMPARE(definition->line, 2U);
        QCOMPARE(definition->offset, std::uint32_t(text.indexOf("total(int")));
    }

    void readsTheRolesOfEachLanguage_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<QList<Role>>("roles"); // of "item"
        QTest::newRow("c") << "c" << QByteArray("void item(void) { item(); }")
                           << QList<Role>{Role::Definition, Role::Call};
        QTest::newRow("java") << "java" << QByteArray("class A { int item() { return item(); } }")
                              << QList<Role>{Role::Definition, Role::Call};
        QTest::newRow("javascript") << "javascript" << QByteArray("function item() { item = 2; }")
                                    << QList<Role>{Role::Definition, Role::Write};
        QTest::newRow("typescript") << "typescript" << QByteArray("interface item {} let x: item = f(`${item}`);")
                                    << QList<Role>{Role::Definition, Role::Reference, Role::Reference};
        QTest::newRow("python") << "python" << QByteArray("import item\ndef item(a):\n    return item(a)  # item\n")
                                << QList<Role>{Role::Import, Role::Definition, Role::Call, Role::Comment};
        QTest::newRow("text") << "cmake" << QByteArray("add_library(item item.cpp)")
                              << QList<Role>{Role::Text, Role::Text};
    }

    void readsTheRolesOfEachLanguage()
    {
        QFETCH(const QString, language);
        QFETCH(const QByteArray, text);
        QFETCH(const QList<Role>, roles);
        OccurrenceScanner scanner;

        QCOMPARE(rolesOf(scanner.scan(language, text), "item"), roles);
    }

    void encodesPostingsByWordInTextOrder()
    {
        using PostingCodec::Posting;
        const QList<Posting> postings{{7, 300, 9, Role::Call},
                                      {2, 40, 1, Role::Definition},
                                      {7, 12, 0, Role::Reference},
                                      {300, 70000, 900, Role::Comment}};

        const QByteArray encoded = PostingCodec::encode(postings);

        QList<Posting> decoded;
        PostingCodec::decode(encoded, [&decoded](const Posting &posting) {
            decoded.append(posting);
            return true;
        });
        QCOMPARE(decoded, (QList<Posting>{postings[1], postings[2], postings[0], postings[3]}));
        QCOMPARE(PostingCodec::find(encoded, 7), (QList<Posting>{postings[2], postings[0]}));
        QVERIFY(PostingCodec::find(encoded, 8).isEmpty());
        QCOMPARE(PostingCodec::wordIds(encoded), (QList<std::uint32_t>{2, 7, 300}));
        QVERIFY(encoded.size() < 30); // a few bytes each
    }

    void findsTheFilesAndOccurrencesOfAWord()
    {
        OccurrenceStore store;
        store.setFiles({{"/p/a.cpp", {10, 1}, {{"total", 4, 0, Role::Definition}, {"sum", 20, 1, Role::Reference}}},
                        {"/p/b.cpp", {10, 1}, {{"total", 8, 2, Role::Call}}}});

        QCOMPARE(setOf(store.filesContaining("total")), (QSet<QString>{"/p/a.cpp", "/p/b.cpp"}));
        QCOMPARE(locationsOf(store, "total"), (QList<OccurrenceStore::Location>{{"/p/a.cpp", 4, 0, Role::Definition},
                                                                                {"/p/b.cpp", 8, 2, Role::Call}}));
        QCOMPARE(store.occurrencesIn("/p/a.cpp").size(), 2);
        QVERIFY(store.filesContaining("Total").isEmpty()); // case-sensitive
    }

    void forgetsTheOldOccurrencesOfAFileIndexedAgain()
    {
        OccurrenceStore store;
        store.setFiles({{"/p/a.cpp", {10, 1}, {{"total", 4, 0, Role::Definition}}}});

        store.setFiles({{"/p/a.cpp", {12, 2}, {{"sum", 4, 0, Role::Definition}}}});
        store.removeFiles({"/p/none.cpp"});

        QVERIFY(store.filesContaining("total").isEmpty());
        QCOMPARE(store.filesContaining("sum"), QStringList{"/p/a.cpp"});
        QCOMPARE(store.fileStates().value("/p/a.cpp"), (OccurrenceStore::FileState{12, 2}));
        QCOMPARE(store.fileCount(), 1);
    }

    void renumbersTheFilesOnceManyAreDead()
    {
        OccurrenceStore store;
        for (int round = 0; round < 3; ++round) {
            QList<OccurrenceStore::IndexedFile> files;
            for (int i = 0; i < 1000; ++i)
                files.append({QString("/p/%1.cpp").arg(i), {round, round}, {{"word", 0, 0, Role::Reference}}});
            store.setFiles(files);
        }

        QCOMPARE(store.filesContaining("word").size(), 1000);
        QCOMPARE(locationsOf(store, "word").size(), 1000);
    }

    void savesAndLoadsTheStore()
    {
        const QTemporaryDir dir;
        OccurrenceStore saved;
        saved.setFiles({{"/p/a.cpp", {10, 1}, {{"total", 4, 0, Role::Definition}}},
                        {"/p/b.py", {20, 2}, {{"total", 1, 0, Role::Import}, {"sum", 9, 1, Role::Write}}}});
        saved.setFiles({{"/p/a.cpp", {11, 3}, {{"product", 4, 0, Role::Definition}}}});
        QString error;
        QVERIFY2(saved.save(dir.filePath("index"), &error), qPrintable(error));

        OccurrenceStore loaded;
        QVERIFY(loaded.load(dir.filePath("index")));

        QCOMPARE(loaded.fileStates(), saved.fileStates());
        QCOMPARE(locationsOf(loaded, "total"), locationsOf(saved, "total"));
        QCOMPARE(loaded.occurrencesIn("/p/a.cpp"), saved.occurrencesIn("/p/a.cpp"));
    }

    void dropsADamagedSave()
    {
        const QTemporaryDir dir;
        write(dir.filePath("index"), "not an index");
        OccurrenceStore store;
        store.setFiles({{"/p/a.cpp", {10, 1}, {{"total", 4, 0, Role::Definition}}}});

        QVERIFY(!store.load(dir.filePath("index")));
        QCOMPARE(store.fileCount(), 0);
    }

    void indexesAFolderThenOnlyWhatChanged()
    {
        const QTemporaryDir dir;
        write(dir.filePath("a.cpp"), "int total;\n");
        write(dir.filePath("src/b.py"), "total = 1\n");
        write(dir.filePath("image.png"), QByteArray("\x89PNG\0\0total", 12));
        OccurrenceIndex index;
        QSignalSpy finished(&index, &OccurrenceIndex::indexingFinished);

        index.setFolder(dir.path());
        QVERIFY(finished.wait());

        QCOMPARE(setOf(index.store().filesContaining("total")),
                 (QSet<QString>{dir.filePath("a.cpp"), dir.filePath("src/b.py")}));
        QVERIFY(QFileInfo::exists(OccurrenceIndex::cachePathFor(dir.path())));

        QFile::remove(dir.filePath("src/b.py"));
        write(dir.filePath("a.cpp"), "int sum;\n");
        index.refresh();
        QVERIFY(finished.wait());

        QVERIFY(index.store().filesContaining("total").isEmpty());
        QCOMPARE(index.store().filesContaining("sum"), QStringList{dir.filePath("a.cpp")});
    }

    void startsFromTheSavedIndex()
    {
        const QTemporaryDir dir;
        write(dir.filePath("a.cpp"), "int total;\n");
        {
            OccurrenceIndex first;
            QSignalSpy finished(&first, &OccurrenceIndex::indexingFinished);
            first.setFolder(dir.path());
            QVERIFY(finished.wait());
        }
        // Changed behind the index's back, with the same size and date: the
        // saved occurrences are kept, which shows they were loaded.
        const QDateTime modified = QFileInfo(dir.filePath("a.cpp")).lastModified();
        write(dir.filePath("a.cpp"), "int other;\n");
        QFile file(dir.filePath("a.cpp"));
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(modified, QFileDevice::FileModificationTime));
        file.close();

        OccurrenceIndex second;
        QSignalSpy finished(&second, &OccurrenceIndex::indexingFinished);
        second.setFolder(dir.path());
        QVERIFY(finished.wait());

        QCOMPARE(second.store().filesContaining("total"), QStringList{dir.filePath("a.cpp")});
    }

    void indexesAnUnsavedText()
    {
        const QTemporaryDir dir;
        OccurrenceIndex index;

        index.updateFile(dir.filePath("a.cpp"), "int typed;\n");

        QTRY_COMPARE(index.store().filesContaining("typed"), QStringList{dir.filePath("a.cpp")});
        QCOMPARE(index.store().fileStates().value(dir.filePath("a.cpp")).modifiedMs, -1);
    }
};

QTEST_MAIN(TestIndex)
#include "tst_index.moc"
