// SPDX-License-Identifier: GPL-3.0-only

#include "ui/AppFonts.h"
#include "ui/Motion.h"
#include "ui/Snackbar.h"
#include "ui/ThemeStyle.h"
#include "ui/UiIcons.h"

#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextLayout>
#include <QToolButton>
#include <QtTest>

namespace {

struct Fixture {
    QWidget window;
    QLineEdit *editor = new QLineEdit(&window);
    Snackbar *snackbar = new Snackbar(&window);

    explicit Fixture(QSize size = QSize(900, 700))
    {
        window.resize(size);
        window.show();
        editor->setFocus();
    }

    QTextEdit *body() const { return snackbar->findChild<QTextEdit *>(); }

    QScrollArea *footer() const { return snackbar->findChild<QScrollArea *>(); }

    QList<QPushButton *> buttons() const { return snackbar->findChildren<QPushButton *>(); }
};

SnackbarMessage permissionMessage()
{
    return {
        .icon = UiIcons::icon(QStringLiteral("terminal"), Theme::kodamiko().accent),
        .title = QStringLiteral("Claude asks to use Bash"),
        .detail = QStringLiteral("cmake --build build-debug --target kodamiko\nctest --test-dir build-debug"),
        .actions = {{QStringLiteral("Deny"), {}},
                    {QStringLiteral("Always Allow"), {}},
                    {QStringLiteral("Allow"), {}, SnackbarAction::Emphasis::Primary}},
    };
}

bool fullyVisible(const QWidget *widget)
{
    return widget->isVisible() && widget->visibleRegion().contains(widget->rect());
}

} // namespace

class TestSnackbar : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        Motion::policy().setEnabled(false);
    }

    void init()
    {
        ThemeStyle::apply(Theme::kodamiko());
        QFont font = AppFonts::interfaceFont();
        font.setPointSizeF(11);
        ThemeStyle::applyFont(font);
    }

    void contentAndActionsStayWithinTheWindow_data()
    {
        QTest::addColumn<QSize>("windowSize");
        QTest::addColumn<int>("repetitions");
        QTest::newRow("short") << QSize(900, 700) << 1;
        QTest::newRow("long") << QSize(900, 700) << 100;
        QTest::newRow("narrow") << QSize(320, 480) << 100;
        QTest::newRow("short-window") << QSize(640, 260) << 100;
        QTest::newRow("small-window") << QSize(320, 300) << 100;
    }

    void contentAndActionsStayWithinTheWindow()
    {
        QFETCH(const QSize, windowSize);
        QFETCH(const int, repetitions);
        const Fixture fixture(windowSize);
        SnackbarMessage message = permissionMessage();
        message.detail = (message.detail + QLatin1Char('\n')).repeated(repetitions);
        const QString expected = message.title + QLatin1Char('\n') + message.detail;
        fixture.snackbar->post(message);
        QCoreApplication::processEvents();

        QCOMPARE(fixture.body()->toPlainText(), expected);
        QCOMPARE(fixture.body()->verticalScrollBar()->value(), 0);
        QVERIFY(fixture.window.rect().contains(fixture.snackbar->geometry()));
        QVERIFY(fullyVisible(fixture.snackbar->findChild<QToolButton *>()));
        for (const QPushButton *button : fixture.buttons())
            QVERIFY2(fullyVisible(button), qPrintable(button->text()));
        if (repetitions > 1)
            QVERIFY(fixture.body()->verticalScrollBar()->maximum() > 0);
        else
            QCOMPARE(fixture.body()->verticalScrollBar()->maximum(), 0);

        const QRect footerBefore = fixture.footer()->geometry();
        fixture.body()->verticalScrollBar()->setValue(fixture.body()->verticalScrollBar()->maximum());
        QCOMPARE(fixture.footer()->geometry(), footerBefore);
    }

    void longTokensAndMarkupRemainIntactAndWrap()
    {
        const Fixture fixture(QSize(320, 480));
        const QString title = QStringLiteral("VeryLongTitle").repeated(60);
        const QString detail = QStringLiteral("<script> & C:\\folder\\") + QStringLiteral("abc123").repeated(1000);
        fixture.snackbar->post({.title = title, .detail = detail, .actions = {{QStringLiteral("Close"), {}}}});
        QCoreApplication::processEvents();

        QCOMPARE(fixture.body()->toPlainText(), title + QLatin1Char('\n') + detail);
        QCOMPARE(fixture.body()->horizontalScrollBar()->maximum(), 0);
        const QTextDocument *document = fixture.body()->document();
        for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
            const QTextLayout *layout = block.layout();
            for (int i = 0; i < layout->lineCount(); ++i)
                QVERIFY(layout->lineAt(i).naturalTextWidth() <= fixture.body()->viewport()->width());
        }
        QVERIFY(fullyVisible(fixture.buttons().first()));
    }

    void actionsStackAndLongLabelsWrapWhenResized()
    {
        Fixture fixture;
        SnackbarMessage message = permissionMessage();
        message.actions[1].text = QStringLiteral("Always allow for this project");
        fixture.snackbar->post(message);
        QCoreApplication::processEvents();
        const auto buttons = fixture.buttons();
        QCOMPARE(buttons.first()->y(), buttons.last()->y());
        const int singleLineHeight = buttons[1]->height();

        fixture.window.resize(280, 480);
        QCoreApplication::processEvents();
        QVERIFY(buttons.first()->geometry().bottom() < buttons[1]->y());
        QVERIFY(buttons[1]->height() > singleLineHeight);
        for (const QPushButton *button : buttons)
            QVERIFY(fullyVisible(button));
        QVERIFY(fixture.window.rect().contains(fixture.snackbar->geometry()));

        fixture.window.resize(900, 700);
        QCoreApplication::processEvents();
        QCOMPARE(buttons.first()->y(), buttons.last()->y());
        QCOMPARE(buttons[1]->height(), singleLineHeight);
    }

    void enlargedFontsKeepActionsReadable()
    {
        const Fixture fixture(QSize(380, 500));
        fixture.snackbar->post(permissionMessage());
        QFont font = QApplication::font();
        font.setPointSizeF(22);
        ThemeStyle::applyFont(font);
        QCoreApplication::processEvents();

        QVERIFY(fixture.window.rect().contains(fixture.snackbar->geometry()));
        for (const QPushButton *button : fixture.buttons()) {
            QVERIFY(fullyVisible(button));
            QVERIFY(button->height() >= button->fontMetrics().height());
        }
        QCOMPARE(fixture.body()->document()->begin().charFormat().font().pointSizeF(), 22.0);
    }

    void exceptionallyManyActionsRemainReachable()
    {
        const Fixture fixture(QSize(320, 300));
        SnackbarMessage message = permissionMessage();
        int chosen = -1;
        message.actions.clear();
        for (int i = 0; i < 20; ++i)
            message.actions.append({QStringLiteral("Action %1").arg(i), [&chosen, i] { chosen = i; }});
        fixture.snackbar->post(message);
        QCoreApplication::processEvents();
        QVERIFY(fixture.window.rect().contains(fixture.snackbar->geometry()));
        QVERIFY(fixture.footer()->verticalScrollBar()->maximum() > 0);
        QPushButton *last = fixture.buttons().last();
        fixture.footer()->ensureWidgetVisible(last);
        QVERIFY(fullyVisible(last));
        QTest::mouseClick(last, Qt::LeftButton);
        QCOMPARE(chosen, 19);
        QCOMPARE(fixture.snackbar->count(), 0);
    }

    void postingPreservesFocusAndActionsRunOnce()
    {
        Fixture fixture;
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.window));
        fixture.editor->setFocus();
        QTRY_VERIFY(fixture.editor->hasFocus());
        int answers = 0;
        int dismissed = 0;
        fixture.snackbar->post({.title = QStringLiteral("Permission"),
                                .actions = {{QStringLiteral("Allow"), [&answers] { ++answers; }}},
                                .dismissed = [&dismissed] { ++dismissed; }});
        QVERIFY(fixture.editor->hasFocus());
        fixture.snackbar->post({.title = QStringLiteral("Next")});
        QTest::mouseClick(fixture.buttons().first(), Qt::LeftButton);
        QCOMPARE(answers, 1);
        QCOMPARE(dismissed, 0);
        QCOMPARE(fixture.snackbar->shownTitle(), QStringLiteral("Next"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(fixture.buttons().isEmpty());
    }

    void theNextShortMessageResetsScrollingAndShrinks()
    {
        const Fixture fixture;
        SnackbarMessage message = permissionMessage();
        message.detail = message.detail.repeated(100);
        const auto id = fixture.snackbar->post(message);
        fixture.snackbar->post({.title = QStringLiteral("Saved")});
        QCoreApplication::processEvents();
        fixture.body()->verticalScrollBar()->setValue(fixture.body()->verticalScrollBar()->maximum());
        const int longHeight = fixture.snackbar->height();

        fixture.snackbar->withdraw(id);
        QCoreApplication::processEvents();

        QCOMPARE(fixture.body()->toPlainText(), QStringLiteral("Saved"));
        QCOMPARE(fixture.body()->verticalScrollBar()->value(), 0);
        QCOMPARE(fixture.body()->verticalScrollBar()->maximum(), 0);
        QVERIFY(fixture.footer()->isHidden());
        QVERIFY(fixture.snackbar->height() < longHeight);
        QVERIFY(fixture.window.rect().contains(fixture.snackbar->geometry()));
    }

    void resizingWhileEnteringKeepsTheSnackbarInside()
    {
        Fixture fixture;
        Motion::policy().setEnabled(true);
        fixture.snackbar->post(permissionMessage());
        fixture.window.resize(320, 300);
        QTest::qWait(int(Motion::enterDuration.count()) + 30);

        QVERIFY(fixture.window.rect().contains(fixture.snackbar->geometry()));
        for (const QPushButton *button : fixture.buttons())
            QVERIFY(fullyVisible(button));
        Motion::policy().setEnabled(false);
    }
};

QTEST_MAIN(TestSnackbar)
#include "tst_snackbar.moc"
