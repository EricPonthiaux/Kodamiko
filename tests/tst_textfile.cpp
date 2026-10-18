#include "editor/TextFile.h"

#include <QtTest>

class TestTextFile : public QObject {
    Q_OBJECT

private slots:

    void roundTripsBytesExactly_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("lf") << QByteArray("a\nb\n");
        QTest::newRow("crlf") << QByteArray("a\r\nb\r\n");
        QTest::newRow("mixed") << QByteArray("a\r\nb\nc");
        QTest::newRow("bom") << QByteArray("\xEF\xBB\xBFint x;\n");
        QTest::newRow("utf8") << QByteArray("// é → ✓\n");
        QTest::newRow("latin1") << QByteArray("caf\xE9\n");
    }

    void roundTripsBytesExactly()
    {
        QFETCH(const QByteArray, bytes);
        const DecodedText decoded = TextFile::decode(bytes);
        QCOMPARE(TextFile::encode(decoded.text, decoded.format), bytes);
    }

    void detectsFormat()
    {
        QCOMPARE(TextFile::decode("a\r\nb").format.lineEnding, LineEnding::CrLf);
        QCOMPARE(TextFile::decode("a\nb").format.lineEnding, LineEnding::Lf);
        QVERIFY(TextFile::decode("\xEF\xBB\xBFx").format.hasUtf8Bom);
        QVERIFY(TextFile::decode("caf\xE9").format.isLatin1);
        QCOMPARE(TextFile::decode("caf\xE9").text, QStringLiteral("café"));
    }

    void refusesBinaryFiles()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        file.write(QByteArray("\x7F"
                              "ELF\0\0\0",
                              7));
        file.close();
        QString error;
        QVERIFY(!TextFile::load(file.fileName(), &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestTextFile)
#include "tst_textfile.moc"
