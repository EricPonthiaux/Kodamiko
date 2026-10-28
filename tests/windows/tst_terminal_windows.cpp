// SPDX-License-Identifier: GPL-3.0-only

#include "terminal/IPty.h"
#include "terminal/ProcessSpec.h"
#include "terminal/VtScreen.h"

#include <QSignalSpy>
#include <QtTest>

// Windows' PTY (ConPTY): real programs in a pseudo-console.
class TestTerminalWindows : public QObject {
    Q_OBJECT

private slots:

    void runsAProgramInAPty()
    {
        auto pty = createPty();
        QByteArray output;
        connect(pty.get(), &IPty::dataReceived, this, [&output](const QByteArray &data) { output += data; });
        QSignalSpy finished(pty.get(), &IPty::finished);
        ProcessSpec spec;
        spec.environment = terminalEnvironment();
        spec.program = "cmd.exe";
        spec.arguments = {"/c", "echo kodamiko-ok"};

        QVERIFY2(pty->start(spec, QSize(91, 33)), qPrintable(pty->errorString()));

        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        // Into the pseudo-console, even when Kodamiko's own output is redirected
        // (ctest's pipe).
        QVERIFY(output.contains("kodamiko-ok"));
    }

    // AltGr arrives as Ctrl+Alt: its character is text, not Ctrl+letter
    // nor an Alt escape.
    void typesWhatAltGrComposes_data()
    {
        QTest::addColumn<int>("key");
        QTest::addColumn<QString>("text");
        QTest::newRow("letter") << int(Qt::Key_E) << QStringLiteral("€");
        QTest::newRow("symbol") << int(Qt::Key_BraceLeft) << QStringLiteral("{");
    }

    void typesWhatAltGrComposes()
    {
        QFETCH(const int, key);
        QFETCH(const QString, text);
        VtScreen screen(2, 10);
        QByteArray output;
        connect(&screen, &VtScreen::outputReady, this, [&output](const QByteArray &data) { output += data; });

        QVERIFY(screen.sendKey(key, Qt::ControlModifier | Qt::AltModifier, text, OptionKey::Compose));

        QCOMPARE(output, text.toUtf8());
    }
};

QTEST_GUILESS_MAIN(TestTerminalWindows)
#include "tst_terminal_windows.moc"
