// SPDX-License-Identifier: GPL-3.0-only

#include "ui/ToolTipPlacement.h"

#include <QApplication>
#include <QHelpEvent>
#include <QScreen>
#include <QToolTip>
#include <QtTest>

namespace {

const QString wideText = QStringLiteral("A tooltip as wide as the path of a file deep in a project's tree");

QWidget *visibleToolTip()
{
    const QWidgetList windows = QApplication::topLevelWidgets();
    const auto tip = std::ranges::find_if(
        windows, [](const QWidget *window) { return window->windowType() == Qt::ToolTip && window->isVisible(); });
    return tip == windows.end() ? nullptr : *tip;
}

// A point near the right edge of the screen, where a wide tooltip overflows.
QPoint nearTheRightEdge()
{
    const QRect area = QGuiApplication::primaryScreen()->geometry();
    return {area.right() - 50, area.top() + 100};
}

void sendHelpEvent(QWidget *widget, QPoint globalPosition)
{
    QHelpEvent event(QEvent::ToolTip, widget->mapFromGlobal(globalPosition), globalPosition);
    QApplication::sendEvent(widget, &event);
}

} // namespace

class TestToolTips : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        ToolTipPlacement::install();
        ToolTipPlacement::install(); // installation is shared by all windows
        window_.resize(400, 300);
        window_.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window_));
    }

    void cleanup()
    {
        QToolTip::hideText();
        QTRY_VERIFY(!visibleToolTip());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void aWideToolTipNearTheRightEdgeSlidesLeftUnderTheMouse()
    {
        window_.setToolTip(wideText);
        const QPoint mouse = nearTheRightEdge();

        sendHelpEvent(&window_, mouse);

        QTRY_VERIFY(visibleToolTip());
        const QRect tip = visibleToolTip()->frameGeometry();
        const QRect area = QGuiApplication::primaryScreen()->geometry();
        QVERIFY2(tip.left() <= mouse.x() && tip.right() >= mouse.x(),
                 qPrintable(QStringLiteral("tip %1..%2, mouse at %3").arg(tip.left()).arg(tip.right()).arg(mouse.x())));
        QCOMPARE(tip.right(), area.right());
    }

    void aToolTipWithRoomOnTheRightStartsBesideTheMouse()
    {
        window_.setToolTip(QStringLiteral("Short"));
        const QPoint mouse = QGuiApplication::primaryScreen()->geometry().topLeft() + QPoint(40, 100);

        sendHelpEvent(&window_, mouse);

        QTRY_VERIFY(visibleToolTip());
        QCOMPARE(visibleToolTip()->x(), mouse.x() + 2);
    }

    void aToolTipShownByCodeKeepsQtsPlacement()
    {
        window_.setToolTip({});
        sendHelpEvent(&window_, QGuiApplication::primaryScreen()->geometry().topLeft() + QPoint(40, 100));
        QCoreApplication::processEvents();
        const QPoint anchor = nearTheRightEdge();

        QToolTip::showText(anchor, wideText, &window_);

        QTRY_VERIFY(visibleToolTip());
        QVERIFY(visibleToolTip()->geometry().right() < anchor.x());
    }

private:
    QWidget window_;
};

QTEST_MAIN(TestToolTips)
#include "tst_tooltips.moc"
