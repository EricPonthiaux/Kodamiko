#include "terminal/TerminalWidget.h"
#include "terminal/IPty.h"

#include <QImage>
#include <QtTest>

// Benchmark of the terminal's painting: a full screen of colored text, as
// a TUI like Claude Code draws it.
class TestTerminalPaint : public QObject {
    Q_OBJECT

private slots:
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
        QBENCHMARK {
            widget.viewport()->render(&image);
        }
#endif
    }
};

QTEST_MAIN(TestTerminalPaint)
#include "tst_terminalpaint.moc"
