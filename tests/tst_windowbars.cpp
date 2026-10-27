// SPDX-License-Identifier: GPL-3.0-only

#include "editor/EditorStatus.h"
#include "editor/EditorWidget.h"
#include "ui/AppFonts.h"
#include "ui/ElidingToolButton.h"
#include "ui/ModeTabBar.h"
#include "ui/ThemeStyle.h"
#include "ui/TitleBar.h"
#include "ui/UiIcons.h"
#include "ui/UiMetrics.h"
#include "ui/WindowControls.h"
#include "ui/WindowFrame.h"
#include "ui/WorkspaceStatusBar.h"

#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {

struct Window {
    QMainWindow root;
    TitleBar *title = new TitleBar(&root);
    QTabBar *modes = new ModeTabBar(&root);
    WorkspaceStatusBar *status = nullptr;
    QAction run{"Run", &root};
    QAction stop{"Stop", &root};
    QAction search{"Go to File", &root};
    QAction settings{"Preferences", &root};
    QMenu projectMenu{&root};
    QMenu runMenu{&root};
    ElidingToolButton *branch = new ElidingToolButton(&root, 220);
    QToolButton *cancel = new QToolButton(&root);

    explicit Window(const Theme &theme)
    {
        root.setMenuWidget(title);
        title->applyTheme(theme);
        for (const char *name : {"&File", "&Edit", "&View", "&Navigate", "&Claude", "&Git", "&Run", "&Help"})
            title->menuBar()->addMenu(name)->addAction("Action");
        projectMenu.addAction("Switch Project");
        title->setProject("a-project-with-a-very-long-name-for-the-workspace", "/projects/long-project", &projectMenu);
        runMenu.addAction("Build");
        title->setRunActions(&run, &stop);
        title->setRunConfiguration({.icon = UiIcons::icon("build", theme.foreground),
                                    .name = "CMake: build a-very-long-configuration-name",
                                    .toolTip = "Build the selected configuration",
                                    .menu = &runMenu});
        title->addActionButton(&search, "search");
        title->addActionButton(&settings, "settings");

        modes->setObjectName("modeTabs");
        modes->setExpanding(false);
        modes->setDrawBase(false);
        modes->setUsesScrollButtons(false);
        modes->setElideMode(Qt::ElideNone);
        modes->setShape(QTabBar::RoundedSouth);
        const QStringList labels{"Terminal", "Edit", "Diff", "History"};
        const QStringList icons{"terminal", "code", "diff", "history"};
        for (int i = 0; i < labels.size(); ++i) {
            modes->addTab(UiIcons::icon(icons.at(i), theme.mutedText), labels.at(i));
            modes->setTabToolTip(i, labels.at(i));
        }
        modes->setCurrentIndex(1);
        status = new WorkspaceStatusBar(modes, &root);
        root.setStatusBar(status);
        auto *position = new QToolButton(&root);
        position->setText("128:24");
        auto *format = new QToolButton(&root);
        format->setText("UTF-8  ·  LF  ·  4 spaces");
        auto *usage = new QLabel("Context: 24%", &root);
        status->addGroup("Agent Usage", {usage});
        status->addGroup("Editor", {position, format});
        branch->setObjectName("chip");
        branch->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        branch->setIcon(UiIcons::icon("branch", theme.mutedText));
        branch->setText("feature/a-very-long-branch-name-for-responsive-layout ↑2");
        branch->setToolTip(branch->text());
        cancel->setText("Stop Git");
        cancel->hide();
        status->addGroup("Git", {branch, cancel});
        auto *body = new QLabel("Editor workspace", &root);
        body->setAlignment(Qt::AlignCenter);
        root.setCentralWidget(body);
    }

    void show(int width)
    {
        root.resize(width, 220);
        root.show();
    }
};

bool isInside(const QWidget *child, const QWidget *ancestor)
{
    return ancestor->rect().contains(QRect(child->mapTo(ancestor, QPoint()), child->size()));
}

} // namespace

class TestWindowBars : public QObject {
    Q_OBJECT

private slots:

    void init()
    {
        QFont font = AppFonts::interfaceFont();
        font.setPointSize(11);
        QApplication::setFont(font);
        ThemeStyle::apply(Theme::duet());
    }

    void barsKeepControlsInsideTheWindow_data()
    {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("fontSize");
        QTest::addColumn<bool>("dark");
        QTest::newRow("dark-11pt-480px") << 480 << 11 << true;
        QTest::newRow("light-15pt-480px") << 480 << 15 << false;
        for (bool dark : {true, false}) {
            for (int fontSize : {11, 15, 22}) {
                for (int width : {640, 800, 1024, 1440, 1920}) {
                    const QByteArray name =
                        QStringLiteral("%1-%2pt-%3px").arg(dark ? "dark" : "light").arg(fontSize).arg(width).toUtf8();
                    QTest::newRow(name.constData()) << width << fontSize << dark;
                }
            }
        }
    }

    void barsKeepControlsInsideTheWindow()
    {
        QFETCH(int, width);
        QFETCH(int, fontSize);
        QFETCH(bool, dark);
        QFont font = QApplication::font();
        font.setPointSize(fontSize);
        QApplication::setFont(font);
        const Theme theme = dark ? Theme::duet() : Theme::named("duet-paper");
        ThemeStyle::apply(theme);
        Window window(theme);
        window.show(width);
        QVERIFY(QTest::qWaitForWindowExposed(&window.root));
        QTest::qWait(20);
        QCOMPARE(window.root.width(), width);
        auto *project = window.title->findChild<QToolButton *>("projectChip");
        auto *run = window.title->findChild<QToolButton *>("runConfiguration");
        QVERIFY(isInside(project, window.title));
        QVERIFY(isInside(run, window.title));
        QVERIFY(isInside(window.title->windowButton(WindowButton::Kind::Close), window.title));
        const auto children = window.title->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
        for (const QWidget *left : children) {
            if (!left->isVisible() || left->isWindow())
                continue;
            QVERIFY2(isInside(left, window.title), qPrintable(left->objectName()));
            for (const QWidget *right : children) {
                if (right == left || !right->isVisible() || right->isWindow())
                    continue;
                QVERIFY2(!left->geometry().intersects(right->geometry()),
                         qPrintable(left->objectName() + "/" + right->objectName()));
            }
        }
        QVERIFY(isInside(window.modes, window.status));
        QVERIFY(window.status->width() - window.modes->mapTo(window.status, window.modes->rect().topRight()).x() < 20);
        for (int i = 0; i < window.modes->count(); ++i) {
            QVERIFY(window.modes->rect().contains(window.modes->tabRect(i)));
            QTest::mouseClick(window.modes, Qt::LeftButton, Qt::NoModifier, window.modes->tabRect(i).center());
            QCOMPARE(window.modes->currentIndex(), i);
        }
        QTest::qWait(20);
        for (int i = 0; i < window.modes->count(); ++i)
            QVERIFY(window.modes->rect().contains(window.modes->tabRect(i)));
        const QString preview = qEnvironmentVariable("DUET_BAR_PREVIEW_DIR");
        if (!preview.isEmpty())
            window.root.grab().save(preview + "/" + QString::fromLatin1(QTest::currentDataTag()) + ".png");
    }

    void boxesAreNoTallerThanATabAndStandInTheMiddleOfTheirBar_data()
    {
        QTest::addColumn<int>("fontSize");
        for (int fontSize : {11, 16, 22})
            QTest::newRow(qPrintable(QStringLiteral("%1pt").arg(fontSize))) << fontSize;
    }

    void boxesAreNoTallerThanATabAndStandInTheMiddleOfTheirBar()
    {
        QFETCH(int, fontSize);
        QFont font = QApplication::font();
        font.setPointSize(fontSize);
        QApplication::setFont(font);
        Window window(Theme::duet());

        window.show(1440);
        QVERIFY(QTest::qWaitForWindowExposed(&window.root));
        QTest::qWait(20);

        const QWidget *project = window.title->findChild<QToolButton *>("projectChip");
        const QWidget *run = window.title->findChild<QToolButton *>("runConfiguration")->parentWidget();
        const QList<std::pair<const QWidget *, const QWidget *>> boxes{
            {project, window.title}, {run, window.title}, {window.modes, window.status}};
        for (const auto &[box, bar] : boxes) {
            const int top = box->mapTo(bar, QPoint()).y();
            const int bottom = bar->height() - top - box->height();
            QVERIFY2(box->height() <= UiMetrics::tabHeight(), qPrintable(box->objectName()));
            QVERIFY2(top > 0 && bottom > 0, qPrintable(box->objectName()));
            QVERIFY2(qAbs(top - bottom) <= 2,
                     qPrintable(QStringLiteral("%1: %2 above, %3 below").arg(box->objectName()).arg(top).arg(bottom)));
        }
    }

    void closeButtonHoverKeepsTheWindowsRoundedCorner()
    {
        QWidget window;
        WindowFrame frame(&window);
        frame.setFrameless(true);
        auto *layout = new QHBoxLayout(&window);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addStretch();
        auto *controls = new WindowControls(&window);
        layout->addWidget(controls, 0, Qt::AlignTop);
        window.resize(400, 200);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        WindowButton *close = controls->button(WindowButton::Kind::Close);

        close->setDown(true); // fully highlighted, as under the mouse
        const QImage image = close->grab().toImage();

        const QColor corner = image.pixelColor(image.width() - 1, 0);
        const QColor middle = image.pixelColor(image.width() / 2, image.height() / 2 + 6);
        QCOMPARE_LT(corner.red() - corner.green(), 100); // not red: cut by the corner
        QCOMPARE_GT(middle.red() - middle.green(), 100);
    }

    void compactMenuKeepsActionsAndRunStopWorking()
    {
        Window window(Theme::duet());
        window.show(640);
        auto *menuButton = window.title->findChild<QToolButton *>("mainMenuButton");
        QTRY_VERIFY(menuButton->isVisible());
        const QSignalSpy searched(&window.search, &QAction::triggered);
        QTimer::singleShot(0, menuButton->menu(), &QMenu::close);
        QTest::mouseClick(menuButton, Qt::LeftButton);
        QVERIFY(menuButton->menu()->actions().contains(&window.search));
        window.search.trigger();
        QCOMPARE(searched.count(), 1);

        const auto buttons = window.title->findChildren<QToolButton *>("runAction");
        QCOMPARE(buttons.size(), 2);
        const QSignalSpy started(&window.run, &QAction::triggered);
        const QSignalSpy stopped(&window.stop, &QAction::triggered);
        QTest::qWait(20);
        QVERIFY(buttons.first()->isVisible());
        const QRect runRect(buttons.first()->mapTo(window.title, QPoint()), buttons.first()->size());
        QTest::mouseClick(buttons.first(), Qt::LeftButton);
        QCOMPARE(started.count(), 1);
        window.title->setRunConfiguration({.icon = UiIcons::icon("build", Theme::duet().foreground),
                                           .name = "CMake: build a-very-long-configuration-name",
                                           .menu = &window.runMenu,
                                           .running = true});
        QTRY_VERIFY(buttons.last()->isVisible());
        QTRY_COMPARE(QRect(buttons.last()->mapTo(window.title, QPoint()), buttons.last()->size()), runRect);
        QTest::mouseClick(buttons.last(), Qt::LeftButton);
        QCOMPARE(stopped.count(), 1);
    }

    void statusOverflowRetainsLiveControlsAndRestoresThemOnExpansion()
    {
        Window window(Theme::duet());
        window.show(640);
        auto *more = window.status->findChild<QToolButton *>("statusOverflow");
        auto *popup = window.status->findChild<QFrame *>("statusPopup");
        QTRY_VERIFY(more->isVisible());
        QTest::mouseClick(more, Qt::LeftButton);
        QTRY_VERIFY(popup->isVisible());
        QVERIFY(window.branch->isVisible());
        QVERIFY(isInside(window.branch, popup));
        QVERIFY(window.cancel->isHidden());
        const QSignalSpy clicked(window.branch, &QToolButton::clicked);
        QTest::mouseClick(window.branch, Qt::LeftButton);
        QCOMPARE(clicked.count(), 1);
        window.cancel->show();
        QTRY_VERIFY(window.cancel->isVisible());
        popup->hide();

        window.root.resize(1920, window.root.height());
        QTRY_VERIFY(!more->isVisible());
        QVERIFY(window.branch->isVisible());
        QVERIFY(window.cancel->isVisible());
        QVERIFY(isInside(window.branch, window.status));
        window.root.resize(640, window.root.height());
        QTRY_VERIFY(more->isVisible());
        QCOMPARE(window.root.width(), 640);
        window.status->showMessage("Building project…");
        QTRY_VERIFY(isInside(window.modes, window.status));
        QCOMPARE(window.status->currentMessage(), QString("Building project…"));
    }

    void longLabelsTakeTheRoomLeftInWideBars()
    {
        Window window(Theme::duet());
        window.show(1920);
        QVERIFY(QTest::qWaitForWindowExposed(&window.root));
        auto *project = window.title->findChild<ElidingToolButton *>("projectChip");
        auto *run = window.title->findChild<ElidingToolButton *>("runConfiguration");
        QTRY_VERIFY(project->width() > project->boundedWidth());
        QTRY_VERIFY(run->width() > run->boundedWidth());
        QTRY_VERIFY(window.branch->width() > window.branch->boundedWidth());
        const int wideProjectWidth = project->width();

        window.root.resize(800, window.root.height());
        QTRY_VERIFY(project->width() < wideProjectWidth);
        QVERIFY(project->width() >= project->boundedWidth());
        QVERIFY(isInside(window.title->windowButton(WindowButton::Kind::Close), window.title));
    }

    void projectAndRunBlockSitInTheMiddleOfAWideBar()
    {
        Window window(Theme::duet());
        window.show(1920);
        QVERIFY(QTest::qWaitForWindowExposed(&window.root));
        const auto *project = window.title->findChild<QToolButton *>("projectChip");
        const auto *runBlock = window.title->findChild<QFrame *>("runBlock");

        const auto groupCenter = [&] { return (project->geometry().left() + runBlock->geometry().right()) / 2; };

        QTRY_VERIFY(qAbs(groupCenter() - window.title->width() / 2) <= 1);
        // Long labels take the room before the centering does: shorter ones
        // leave enough of it at a laptop's width.
        window.title->setProject("duet", "/p/duet", &window.projectMenu);
        window.title->setRunConfiguration({.name = "CMake: build default", .menu = &window.runMenu});
        window.root.resize(1440, window.root.height());
        QTRY_VERIFY(qAbs(groupCenter() - window.title->width() / 2) <= 1);
    }

    void existingWindowAdaptsToFontChangesAndDetachedTerminals()
    {
        Window window(Theme::duet());
        window.show(1920);
        QVERIFY(QTest::qWaitForWindowExposed(&window.root));
        for (int size : {22, 11, 15}) {
            QFont font = QApplication::font();
            font.setPointSize(size);
            QApplication::setFont(font);
            window.root.resize(640, window.root.height());
            QTest::qWait(20);
            QCOMPARE(window.root.width(), 640);
            QVERIFY(isInside(window.title->windowButton(WindowButton::Kind::Close), window.title));
            QVERIFY(isInside(window.modes, window.status));
            window.modes->setTabVisible(0, false);
            QTest::qWait(20);
            QVERIFY(window.modes->rect().contains(window.modes->tabRect(3)));
            window.modes->setTabVisible(0, true);
            window.root.resize(1920, window.root.height());
            QTest::qWait(20);
        }
    }

    void editorFormatMenuWorksFromTheStatusPopup()
    {
        const QTemporaryDir directory;
        QFile file(directory.filePath("example.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("one\r\ntwo\r\n");
        file.close();
        EditorWidget editor(Theme::duet());
        QString error;
        QVERIFY(editor.load(file.fileName(), &error));
        Window window(Theme::duet());
        auto *status = new EditorStatus(&window.root);
        status->setEditor(&editor);
        window.status->addGroup("File Format", {status});
        window.show(480);
        auto *more = window.status->findChild<QToolButton *>("statusOverflow");
        QTRY_VERIFY(more->isVisible());
        QTest::mouseClick(more, Qt::LeftButton);
        QToolButton *lineEnding = nullptr;
        for (QToolButton *button : status->findChildren<QToolButton *>())
            if (button->text() == "CRLF")
                lineEnding = button;
        QVERIFY(lineEnding);
        QTRY_VERIFY(lineEnding->isVisible());
        bool opened = false;
        QTimer::singleShot(0, [&opened] {
            if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                opened = true;
                menu->actions().first()->trigger(); // LF
                menu->close();
            }
        });
        QTest::mouseClick(lineEnding, Qt::LeftButton);
        QVERIFY(opened);
        QCOMPARE(editor.format().lineEnding, LineEnding::Lf);
    }
};

QTEST_MAIN(TestWindowBars)
#include "tst_windowbars.moc"
