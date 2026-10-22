// SPDX-License-Identifier: GPL-3.0-only

#include "ui/ContextMenus.h"

#include <QtTest>

namespace {

const QRect window(0, 0, 1000, 800);
const QSize menu(200, 300);

} // namespace

class TestContextMenus : public QObject {
    Q_OBJECT

private slots:

    void opensBelowAndRightOfTheClickWhenItFits()
    {
        QCOMPARE(ContextMenus::placement(menu, QPoint(100, 100), window), QPoint(100, 100));
    }

    void opensAboveTheClickWhenOnlyTheSpaceAboveFits()
    {
        QCOMPARE(ContextMenus::placement(menu, QPoint(100, 700), window), QPoint(100, 400));
    }

    void opensLeftOfTheClickNearTheRightEdge()
    {
        QCOMPARE(ContextMenus::placement(menu, QPoint(900, 100), window), QPoint(700, 100));
    }

    void centresOnTheClickWhenNeitherSideFits()
    {
        const QSize tall(200, 500);

        QCOMPARE(ContextMenus::placement(tall, QPoint(100, 400), window), QPoint(100, 150));
    }

    void keepsACentredMenuInsideTheWindow()
    {
        const QSize tall(200, 500);

        QCOMPARE(ContextMenus::placement(tall, QPoint(100, 350), QRect(0, 100, 1000, 700)), QPoint(100, 100));
        QCOMPARE(ContextMenus::placement(tall, QPoint(100, 450), QRect(0, 0, 1000, 700)), QPoint(100, 200));
    }

    void startsAtTheWindowTopWhenTheMenuIsTallerThanTheWindow()
    {
        const QSize huge(200, 1000);

        QCOMPARE(ContextMenus::placement(huge, QPoint(100, 400), QRect(0, 50, 1000, 800)), QPoint(100, 50));
    }
};

QTEST_MAIN(TestContextMenus)
#include "tst_contextmenus.moc"
