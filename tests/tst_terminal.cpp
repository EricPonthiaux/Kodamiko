// SPDX-License-Identifier: GPL-3.0-only

#include "system/Host.h"
#include "terminal/TerminalWidget.h"
#include "terminal/VtScreen.h"

#include <QKeyEvent>
#include <QSignalSpy>
#include <QtTest>

class TestTerminal : public QObject {
    Q_OBJECT

private slots:

    void applicationShortcutsPassTheTerminal()
    {
        const auto passes = [](int key, Qt::KeyboardModifiers modifiers, const QString &text = {}) {
            const QKeyEvent event(QEvent::ShortcutOverride, key, modifiers, text);
            return TerminalWidget::isApplicationShortcut(event);
        };
#ifdef Q_OS_MACOS
        // Qt calls the Ctrl key Meta on macOS, and Cmd Control.
        constexpr Qt::KeyboardModifier ctrlKey = Qt::MetaModifier;
        QVERIFY(passes(Qt::Key_R, Qt::ControlModifier)); // Cmd+R, as in Terminal.app
#else
        constexpr Qt::KeyboardModifier ctrlKey = Qt::ControlModifier;
#endif
        // The keys programs use stay in the terminal.
        QVERIFY(!passes(Qt::Key_C, ctrlKey));
        QVERIFY(!passes(Qt::Key_R, ctrlKey));
        QVERIFY(!passes(Qt::Key_B, Qt::AltModifier));
        QVERIFY(!passes(Qt::Key_Escape, Qt::NoModifier));
        QVERIFY(!passes(Qt::Key_Tab, Qt::ShiftModifier));
        QVERIFY(!passes(Qt::Key_A, Qt::NoModifier, "a"));
        // Mode switching and F11, as before.
        QVERIFY(passes(Qt::Key_F11, Qt::NoModifier));
        QVERIFY(passes(Qt::Key_2, Qt::ControlModifier));
#ifndef Q_OS_MACOS
        // Combinations terminal programs do not use go to the application.
        QVERIFY(passes(Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier));
        QVERIFY(passes(Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier));
        QVERIFY(passes(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier));
        QVERIFY(passes(Qt::Key_N, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier));
        QVERIFY(passes(Qt::Key_S, Qt::ControlModifier | Qt::AltModifier));
        // Except the terminal's copy and paste, and AltGr characters.
        QVERIFY(!passes(Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier));
        QVERIFY(!passes(Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier));
        QVERIFY(!passes(Qt::Key_At, Qt::ControlModifier | Qt::AltModifier, "@"));
#endif
    }

    void writesTextAndMovesCursor()
    {
        VtScreen screen(5, 20);
        screen.feed("hello\r\nworld");
        QCOMPARE(screen.lineText(0), QStringLiteral("hello"));
        QCOMPARE(screen.lineText(1), QStringLiteral("world"));
        QCOMPARE(screen.cursorPosition(), QPoint(5, 1));
    }

    void decodesColorsAndAttributes()
    {
        VtScreen screen(3, 20);
        screen.setDefaultColors(Qt::white, Qt::black);
        screen.feed("\x1b[1;38;2;255;0;0mR\x1b[0mn");
        const TerminalCell red = screen.cell(0, 0);
        QCOMPARE(red.text, QStringLiteral("R"));
        QCOMPARE(red.foreground, QColor(255, 0, 0));
        QVERIFY(red.bold);
        QCOMPARE(screen.cell(0, 1).foreground, QColor(Qt::white));
    }

    void handlesWideCharactersAndUtf8()
    {
        VtScreen screen(2, 10);
        screen.feed("é漢x");
        QCOMPARE(screen.cell(0, 0).text, QStringLiteral("é"));
        QCOMPARE(screen.cell(0, 1).width, 2);
        QVERIFY(screen.cell(0, 2).isContinuation);
        QCOMPARE(screen.cell(0, 3).text, QStringLiteral("x"));
    }

    void signalsOncePerFeedWithMergedDamage()
    {
        VtScreen screen(5, 20);
        screen.takeDamage();
        const QSignalSpy changed(&screen, &VtScreen::screenChanged);
        screen.feed("ab\x1b[1mcd\x1b[0m\r\nef");
        QCOMPARE(changed.size(), 1);
        const QRect damage = screen.takeDamage();
        QVERIFY(damage.contains(QPoint(0, 0)));
        QVERIFY(damage.contains(QPoint(1, 1)));
        QVERIFY(screen.takeDamage().isEmpty());
        screen.feed("");
        QCOMPARE(changed.size(), 1); // nothing changed: no signal
    }

    void readsRawRowsLikeCells()
    {
        VtScreen screen(2, 6);
        screen.setDefaultColors(Qt::white, Qt::black);
        screen.feed("\x1b[7mab\x1b[0m  \r\nxy\r\nzz");
        std::vector<VTermScreenCell> cells;
        screen.readRow(-1, cells); // history line, trailing blanks trimmed
        QCOMPARE(cells.size(), size_t(6));
        QCOMPARE(cells[0].chars[0], uint32_t('a'));
        QRgb foreground = 0;
        QRgb background = 0;
        screen.colorsOf(cells[0], foreground, background);
        QCOMPARE(foreground, QColor(Qt::black).rgb()); // reverse video
        QCOMPARE(background, QColor(Qt::white).rgb());
        QCOMPARE(cells[5].chars[0], uint32_t(0));
        screen.colorsOf(cells[5], foreground, background);
        QCOMPARE(background, QColor(Qt::black).rgb());
        QCOMPARE(screen.lineText(-1), QStringLiteral("ab"));
    }

    void feedsLargeOutput()
    {
        QByteArray output;
        for (int line = 0; line < 20000; ++line)
            output +=
                "\x1b[32mline\x1b[0m " + QByteArray::number(line) + " \x1b[1;34mlorem ipsum dolor sit amet\x1b[0m\r\n";
        QBENCHMARK
        {
            VtScreen screen(60, 200);
            screen.feed(output);
        }
    }

    void keepsScrolledOutLines()
    {
        VtScreen screen(2, 10);
        screen.feed("one\r\ntwo\r\nthree\r\nfour");
        QCOMPARE(screen.scrollbackSize(), 2);
        QCOMPARE(screen.lineText(-2), QStringLiteral("one"));
        QCOMPARE(screen.lineText(-1), QStringLiteral("two"));
        QCOMPARE(screen.lineText(0), QStringLiteral("three"));
    }

    void encodesKeys_data()
    {
        QTest::addColumn<int>("key");
        QTest::addColumn<int>("modifiers");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QByteArray>("expected");
        QTest::newRow("letter") << int(Qt::Key_A) << int(Qt::NoModifier) << "a" << QByteArray("a");
        QTest::newRow("accent") << int(Qt::Key_Eacute) << int(Qt::NoModifier) << "é" << QByteArray("é");
        QTest::newRow("enter") << int(Qt::Key_Return) << int(Qt::NoModifier) << "\r" << QByteArray("\r");
        QTest::newRow("escape") << int(Qt::Key_Escape) << int(Qt::NoModifier) << "\x1b" << QByteArray("\x1b");
        QTest::newRow("up") << int(Qt::Key_Up) << int(Qt::NoModifier) << "" << QByteArray("\x1b[A");
        QTest::newRow("shift-tab") << int(Qt::Key_Backtab) << int(Qt::ShiftModifier) << "" << QByteArray("\x1b[Z");
        QTest::newRow("backspace") << int(Qt::Key_Backspace) << int(Qt::NoModifier) << "\b" << QByteArray("\x7f");
#ifndef Q_OS_MACOS
        QTest::newRow("ctrl-c") << int(Qt::Key_C) << int(Qt::ControlModifier) << "\x03" << QByteArray("\x03");
#endif
    }

    void encodesKeys()
    {
        QFETCH(const int, key);
        QFETCH(const int, modifiers);
        QFETCH(const QString, text);
        QFETCH(const QByteArray, expected);

        VtScreen screen(2, 10);
        QByteArray output;
        connect(&screen, &VtScreen::outputReady, this, [&output](const QByteArray &data) { output += data; });
        QVERIFY(screen.sendKey(key, Qt::KeyboardModifiers(modifiers), text, OptionKey::Compose));
        QCOMPARE(output, expected);
    }

    void typesWhatOptionComposesOnMacOSAndSendsAltAsMetaElsewhere()
    {
        VtScreen screen(2, 10);
        QByteArray output;
        connect(&screen, &VtScreen::outputReady, this, [&output](const QByteArray &data) { output += data; });

        QVERIFY(screen.sendKey(Qt::Key_BraceLeft, Qt::AltModifier, QStringLiteral("{"), OptionKey::Compose));

        const bool isMacOS = Host::operatingSystem() == OperatingSystem::MacOS;
        QCOMPARE(output, isMacOS ? QByteArray("{") : QByteArray("\x1b{"));
    }

    void sendsOptionAsMetaWhenAsked()
    {
        VtScreen screen(2, 10);
        QByteArray output;
        connect(&screen, &VtScreen::outputReady, this, [&output](const QByteArray &data) { output += data; });

        QVERIFY(screen.sendKey(Qt::Key_B, Qt::AltModifier, QStringLiteral("b"), OptionKey::Meta));

        QCOMPARE(output, QByteArray("\033b"));
    }

    void letsModeShortcutsThrough()
    {
        const auto shortcut = [](int key, const QString &text, quint32 scanCode, quint32 virtualKey) {
            const QKeyEvent event(QEvent::ShortcutOverride, key, Qt::ControlModifier, scanCode, virtualKey, 0, text);
            return TerminalWidget::isModeShortcut(event);
        };
        QVERIFY(shortcut(Qt::Key_2, QStringLiteral("2"), 0, 0));
        QVERIFY(!shortcut(Qt::Key_W, QStringLiteral("w"), 0, 0));
        // AZERTY: the 2 key produces "é" without Shift.
#ifdef Q_OS_MACOS
        QVERIFY(shortcut(Qt::Key_Eacute, QStringLiteral("é"), 0, 0x13));
#elif defined(Q_OS_WIN)
        QVERIFY(shortcut(Qt::Key_Eacute, QStringLiteral("é"), 0x03, 0));
#else
        QVERIFY(shortcut(Qt::Key_Eacute, QStringLiteral("é"), 11, 0));
        QVERIFY(shortcut(Qt::Key_QuoteDbl, QStringLiteral("\""), 12, 0));
#endif
    }

    void bracketedPasteWhenRequested()
    {
        VtScreen screen(2, 10);
        QByteArray output;
        connect(&screen, &VtScreen::outputReady, this, [&output](const QByteArray &data) { output += data; });
        screen.feed("\x1b[?2004h"); // the program enables bracketed paste
        screen.paste("a\nb");
        QCOMPARE(output, QByteArray("\x1b[200~a\rb\x1b[201~"));
    }

    void sendsTheWheelWhenTheProgramAsks()
    {
        VtScreen screen(5, 20);
        QByteArray output;
        connect(&screen, &VtScreen::outputReady, this, [&output](const QByteArray &data) { output += data; });
        QVERIFY(!screen.isMouseReporting());
        screen.feed("\x1b[?1000h\x1b[?1006h"); // mouse events, SGR encoding
        QVERIFY(screen.isMouseReporting());
        screen.sendWheel(2, 3, true, Qt::NoModifier);
        screen.sendWheel(2, 3, false, Qt::NoModifier);
        QCOMPARE(output, QByteArray("\x1b[<64;4;3M\x1b[<65;4;3M"));
        screen.feed("\x1b[?1000l");
        QVERIFY(!screen.isMouseReporting());
    }
};

QTEST_GUILESS_MAIN(TestTerminal)
#include "tst_terminal.moc"
