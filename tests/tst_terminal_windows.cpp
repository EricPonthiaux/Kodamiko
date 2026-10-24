// SPDX-License-Identifier: GPL-3.0-only

#include "terminal/IPty.h"
#include "terminal/ProcessSpec.h"

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
        spec.arguments = {"/c", "echo duet-ok"};

        QVERIFY2(pty->start(spec, QSize(91, 33)), qPrintable(pty->errorString()));

        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        // Into the pseudo-console, even when Duet's own output is redirected
        // (ctest's pipe).
        QVERIFY(output.contains("duet-ok"));
    }
};

QTEST_GUILESS_MAIN(TestTerminalWindows)
#include "tst_terminal_windows.moc"
