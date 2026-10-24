// SPDX-License-Identifier: GPL-3.0-only

#include "terminal/IPty.h"
#include "terminal/ProcessSpec.h"

#include <QSignalSpy>
#include <QtTest>

// The Unix PTY (forkpty): real programs in a real terminal.
class TestTerminalUnix : public QObject {
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
        spec.program = "/bin/sh";
        spec.arguments = {"-c", "echo duet-ok; stty size; exit 3"};

        QVERIFY2(pty->start(spec, QSize(91, 33)), qPrintable(pty->errorString()));

        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(output.contains("duet-ok"));
        QVERIFY(output.contains("33 91")); // the grid size is passed on to the program
        QCOMPARE(finished.first().first().toInt(), 3);
    }

    void seesTheCommandAShellRunsInTheForeground()
    {
        auto pty = createPty();
        ProcessSpec spec;
        spec.environment = terminalEnvironment();
        spec.program = "/bin/sh";
        // Interactive, as in a terminal tab: job control is on, the command
        // typed gets its own process group and the terminal.
        spec.arguments = {"-i"};

        QVERIFY2(pty->start(spec, QSize(80, 24)), qPrintable(pty->errorString()));
        pty->write("sleep 30\n");

        QTRY_VERIFY_WITH_TIMEOUT(pty->hasForegroundJob(), 5000);
    }

    void seesNoJobWhileTheProgramStartedHoldsTheTerminal()
    {
        auto pty = createPty();
        ProcessSpec spec;
        spec.environment = terminalEnvironment();
        spec.program = "/bin/sh";
        spec.arguments = {"-c", "sleep 30; true"};

        QVERIFY2(pty->start(spec, QSize(80, 24)), qPrintable(pty->errorString()));
        QTest::qWait(300); // time for sleep to start

        QVERIFY(!pty->hasForegroundJob());
    }
};

QTEST_GUILESS_MAIN(TestTerminalUnix)
#include "tst_terminal_unix.moc"
