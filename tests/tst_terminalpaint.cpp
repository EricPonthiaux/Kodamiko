// SPDX-License-Identifier: GPL-3.0-only

#include "terminal/ProcessSpec.h"
#include "terminal/TerminalWidget.h"

#include <QImage>
#include <QScrollBar>
#include <QtTest>

// Benchmark of the terminal's painting: a full screen of colored text, as
// a TUI like Claude Code draws it.
class TestTerminalPaint : public QObject {
    Q_OBJECT

private slots:

    void scrollBarShowsTheHistory()
    {
#ifdef Q_OS_WIN
        QSKIP("Uses a POSIX shell");
#else
        TerminalWidget widget;
        widget.resize(800, 400);
        widget.show();
        ProcessSpec spec;
        spec.environment = terminalEnvironment();
        spec.program = "/bin/sh";
        spec.arguments = {"-c", "i=0; while [ $i -lt 200 ]; do echo line $i; i=$((i+1)); done; sleep 30"};
        QVERIFY(widget.start(spec));
        QScrollBar *bar = widget.verticalScrollBar();
        QTRY_VERIFY(bar->maximum() > 100);
        QTRY_VERIFY(bar->isVisible());
        QCOMPARE(bar->value(), bar->maximum()); // at the bottom

        // Dragging the bar up shows the history.
        bar->setValue(0);
        QCOMPARE(bar->value(), 0);
        bar->setValue(bar->maximum());
#endif
    }

    void keepsTheHistoryInPlaceWhileOutputArrives()
    {
#ifdef Q_OS_WIN
        QSKIP("Uses a POSIX shell");
#else
        TerminalWidget widget;
        widget.resize(800, 400);
        widget.show();
        ProcessSpec spec;
        spec.environment = terminalEnvironment();
        spec.program = "/bin/sh";
        spec.arguments = {"-c", "i=0; while [ $i -lt 100 ]; do echo line $i; i=$((i+1)); done; sleep 1; "
                                "while [ $i -lt 150 ]; do echo line $i; i=$((i+1)); done; sleep 30"};
        QVERIFY(widget.start(spec));
        QScrollBar *bar = widget.verticalScrollBar();
        QTRY_VERIFY(bar->maximum() > 70);
        const int firstMaximum = bar->maximum();
        const int readingAt = 10;
        bar->setValue(readingAt);

        QTRY_VERIFY(bar->maximum() >= firstMaximum + 50);
        QCOMPARE(bar->value(), readingAt); // the lines read did not move
#endif
    }

    void paintsAFullScreenOfBoxDrawing()
    {
#ifdef Q_OS_WIN
        QSKIP("Uses a POSIX shell");
#else
        TerminalWidget widget;
        widget.resize(1600, 1000);
        widget.show();

        // Frames as Claude Code draws them: ╭───╮ │ … │ ╰───╯
        ProcessSpec spec;
        spec.environment = terminalEnvironment();
        spec.program = "/bin/sh";
        spec.arguments = {"-c", "top='\342\225\255'; bottom='\342\225\260'; dash='\342\224\200'; "
                                "bar='\342\224\202'; line=''; j=0; while [ $j -lt 150 ]; do line=\"$line$dash\"; "
                                "j=$((j+1)); done; i=0; while [ $i -lt 20 ]; do "
                                "printf \"\033[2m$top$line\033[0m\n\"; "
                                "printf \"$bar \033[1mClaude\033[0m answer $i with some text $bar\n\"; "
                                "printf \"\033[2m$bottom$line\033[0m\n\"; i=$((i+1)); done; sleep 30"};
        QVERIFY(widget.start(spec));
        QTest::qWait(500);

        QImage image(widget.viewport()->size(), QImage::Format_ARGB32_Premultiplied);
        QBENCHMARK
        {
            widget.viewport()->render(&image);
        }
#endif
    }

    void paintsAFullScreen()
    {
#ifdef Q_OS_WIN
        QSKIP("Uses a POSIX shell");
#else
        TerminalWidget widget;
        widget.resize(1600, 1000);
        widget.show();

        ProcessSpec spec;
        spec.environment = terminalEnvironment();
        spec.program = "/bin/sh";
        spec.arguments = {"-c", "i=0; while [ $i -lt 200 ]; do "
                                "printf '\\033[32mline\\033[0m %d \\033[1;34mlorem ipsum\\033[0m dolor sit amet, "
                                "\\033[7mconsectetur\\033[0m adipiscing elit \\342\\224\\200\\342\\224\\200 sed do "
                                "eiusmod tempor incididunt ut labore et dolore magna aliqua\\n' $i; i=$((i+1)); done; "
                                "sleep 30"};
        QVERIFY(widget.start(spec));
        QTest::qWait(500);

        QImage image(widget.viewport()->size(), QImage::Format_ARGB32_Premultiplied);
        QBENCHMARK
        {
            widget.viewport()->render(&image);
        }
#endif
    }
};

QTEST_MAIN(TestTerminalPaint)
#include "tst_terminalpaint.moc"
