#include "ui/HoverProgress.h"
#include "ui/Motion.h"
#include "ui/OverlayPopup.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QtTest>

#include <memory>

class TestMotion : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("DuetTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_motion"));
        QSettings().clear();
        Motion::install();
        Motion::install(); // installation is shared by all windows
    }

    void init() { Motion::policy().setEnabled(true); }

    void cleanup()
    {
        Motion::policy().setEnabled(false);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void aMenuAcceptsKeyboardInputWhileEntering()
    {
        QWidget window;
        window.resize(800, 600);
        window.show();
        QMenu menu(&window);
        QAction *first = menu.addAction("First");
        QAction *second = menu.addAction("Second");
        const QSignalSpy triggered(second, &QAction::triggered);
        menu.popup(window.mapToGlobal(QPoint(60, 60)));
        const QRect placed = menu.geometry();
        QVERIFY(menu.graphicsEffect());
        menu.setActiveAction(first);

        QTest::keyClick(&menu, Qt::Key_Down);
        QCOMPARE(menu.activeAction(), second);
        QCOMPARE(menu.geometry(), placed);
        QTest::keyClick(&menu, Qt::Key_Return);

        QCOMPARE(triggered.count(), 1);
        QVERIFY(!menu.isVisible());
        QTRY_VERIFY(window.findChildren<QLabel *>("motionExit").isEmpty());
    }

    void execReturnsWithoutWaitingForTheExit()
    {
        QWidget window;
        window.resize(800, 600);
        window.show();
        QMenu menu(&window);
        QAction *action = menu.addAction("Choose");
        QTimer::singleShot(0, &menu, [&menu, action] {
            menu.setActiveAction(action);
            QTest::keyClick(&menu, Qt::Key_Return);
        });

        QCOMPARE(menu.exec(window.mapToGlobal(QPoint(80, 80))), action);
        QVERIFY(!menu.isVisible());
    }

    void submenusKeepTheirKeyboardNavigation()
    {
        QWidget window;
        window.resize(800, 600);
        window.show();
        QMenu menu(&window);
        QMenu *submenu = menu.addMenu("More");
        QAction *action = submenu->addAction("Nested action");
        const QSignalSpy triggered(action, &QAction::triggered);
        menu.popup(window.mapToGlobal(QPoint(80, 80)));
        menu.setActiveAction(submenu->menuAction());

        QTest::keyClick(&menu, Qt::Key_Right);
        QTRY_VERIFY(submenu->isVisible());
        QVERIFY(submenu->graphicsEffect());
        submenu->setActiveAction(action);
        QTest::keyClick(submenu, Qt::Key_Return);

        QCOMPARE(triggered.count(), 1);
        QVERIFY(!menu.isVisible());
        QVERIFY(!submenu->isVisible());
    }

    void comboBoxesKeepTheirSelectionBehavior()
    {
        QWidget window;
        window.resize(800, 600);
        QComboBox combo(&window);
        combo.addItems({"One", "Two", "Three"});
        combo.move(80, 80);
        window.show();
        combo.showPopup();
        const QWidget *popup = QApplication::activePopupWidget();
        QVERIFY(popup);
        QVERIFY(popup->graphicsEffect());

        QTest::keyClick(combo.view(), Qt::Key_Down);
        QTest::keyClick(combo.view(), Qt::Key_Return);

        QCOMPARE(combo.currentText(), QString("Two"));
        QVERIFY(!popup->isVisible());
    }

    void dialogsCanBeRejectedWhileEntering()
    {
        QWidget window;
        window.resize(800, 600);
        window.show();
        QDialog dialog(&window);
        dialog.resize(300, 200);
        dialog.show();
        QVERIFY(dialog.graphicsEffect());

        QTest::keyClick(&dialog, Qt::Key_Escape);

        QVERIFY(!dialog.isVisible());
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
    }

    void disablingMotionFinishesRunningTransitions()
    {
        QWidget window;
        window.resize(1000, 700);
        window.show();
        OverlayPopup popup("policy", QSizeF(0.5, 0.5), QSize(300, 200), &window);
        popup.showCentered();
        QVERIFY(popup.graphicsEffect());
        const QRect geometry = popup.geometry();

        Motion::policy().setEnabled(false);

        QTRY_VERIFY(!popup.graphicsEffect());
        QCOMPARE(popup.geometry(), geometry);
        QVERIFY(popup.isVisible());
        popup.dismiss();
        QVERIFY(window.findChildren<QLabel *>("motionExit").isEmpty());
        popup.showCentered();
        QVERIFY(!popup.graphicsEffect());
        Motion::policy().setEnabled(true);
        popup.dismiss();
        QVERIFY(!window.findChildren<QLabel *>("motionExit").isEmpty());
        Motion::policy().setEnabled(false);
        QTRY_VERIFY(window.findChildren<QLabel *>("motionExit").isEmpty());
    }

    void reopeningCancelsThePreviousExit()
    {
        QWidget window;
        window.resize(1000, 700);
        window.show();
        OverlayPopup popup("reopen", QSizeF(0.5, 0.5), QSize(300, 200), &window);
        popup.showCentered();
        QTRY_VERIFY(!popup.graphicsEffect());
        popup.dismiss();
        QVERIFY(!window.findChildren<QLabel *>("motionExit").isEmpty());

        popup.showCentered();

        for (const QLabel *ghost : window.findChildren<QLabel *>("motionExit"))
            QVERIFY(!ghost->isVisible());
        QTRY_VERIFY(!popup.graphicsEffect());
        QVERIFY(popup.isVisible());
    }

    void preservesAnExistingGraphicsEffect()
    {
        QWidget window;
        window.resize(1000, 700);
        window.show();
        OverlayPopup popup("shadow", QSizeF(0.5, 0.5), QSize(300, 200), &window);
        auto *shadow = new QGraphicsDropShadowEffect(&popup);
        popup.setGraphicsEffect(shadow);

        popup.showCentered();
        QCOMPARE(popup.graphicsEffect(), shadow);
        Motion::policy().setEnabled(false);
        QCOMPARE(popup.graphicsEffect(), shadow);
        popup.dismiss();
        QVERIFY(window.findChildren<QLabel *>("motionExit").isEmpty());
    }

    void hoverFeedbackBecomesImmediateWhenDisabled()
    {
        QWidget widget;
        HoverProgress hover(&widget);
        hover.setHovered(true);
        Motion::policy().setEnabled(false);
        QCOMPARE(hover.progress(), 1.0);

        hover.setHovered(false);
        QCOMPARE(hover.progress(), 0.0);
        hover.setHovered(true);
        QCOMPARE(hover.progress(), 1.0);
    }

    void deletingAPopupDuringItsEntranceIsSafe()
    {
        QWidget window;
        window.resize(800, 600);
        window.show();
        auto menu = std::make_unique<QMenu>(&window);
        menu->addAction("Transient");
        menu->popup(window.mapToGlobal(QPoint(80, 80)));
        QVERIFY(menu->graphicsEffect());

        menu.reset();
        Motion::policy().setEnabled(false);
        QCoreApplication::processEvents();

        QVERIFY(window.isVisible());
    }
};

QTEST_MAIN(TestMotion)
#include "tst_motion.moc"
