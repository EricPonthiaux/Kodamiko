// SPDX-License-Identifier: GPL-3.0-only

#include "agent/AgentIndicator.h"
#include "diagnostics/ProblemStore.h"
#include "diagnostics/ProblemsView.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "git/GitProcess.h"
#include "lsp/LanguageServers.h"
#include "ui/AppFonts.h"
#include "ui/ClickableCursors.h"
#include "ui/DetachablePage.h"
#include "ui/FileIcons.h"
#include "ui/HoverCloseButtons.h"
#include "ui/OverlayPopup.h"
#include "ui/TabBarBaseLine.h"
#include "ui/ThemeStyle.h"
#include "ui/TitleBar.h"
#include "ui/TrailingLayout.h"
#include "ui/TrailingTabButton.h"
#include "ui/TreeMetrics.h"
#include "ui/ViewHeader.h"
#include "ui/WindowFrame.h"
#include "workspace/ChoicePopup.h"
#include "workspace/CommandPalette.h"
#include "workspace/EntryPopups.h"
#include "workspace/FileOperations.h"
#include "workspace/FileTreeView.h"
#include "workspace/FilesPanel.h"
#include "workspace/ProjectTreeModel.h"
#include "workspace/SidePanel.h"

#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QSizeGrip>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

namespace {

void write(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
}

// Rows of the model under `parent`, by name, nested ones in parentheses.
QStringList rowsOf(ProjectTreeModel &model, const QModelIndex &parent)
{
    QStringList rows;
    if (model.canFetchMore(parent))
        model.fetchMore(parent);
    for (int row = 0; row < model.rowCount(parent); ++row) {
        const QModelIndex index = model.index(row, 0, parent);
        QString text = index.data().toString();
        if (!model.isDir(index) && model.rowCount(index) > 0)
            text += "(" + rowsOf(model, index).join(",") + ")";
        rows.append(text);
    }
    return rows;
}

} // namespace

class TestWorkspace : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // Settings of the tests, not the user's. Without an organization,
        // Windows' registry keeps no settings at all.
        QCoreApplication::setOrganizationName(QStringLiteral("KDKTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_workspace"));
        ClickableCursors::install();
    }

    // The widgets are children: a window always keeps a cursor of its own.
    void clickableControlsShowThePointingHand()
    {
        QWidget window;
        const QToolButton button(&window);
        const QComboBox combo(&window);
        QToolButton disabled(&window);
        disabled.setEnabled(false);
        QToolButton ownCursor(&window);
        ownCursor.setCursor(Qt::CrossCursor);

        window.ensurePolished();

        QCOMPARE(button.cursor().shape(), Qt::PointingHandCursor);
        QCOMPARE(combo.cursor().shape(), Qt::PointingHandCursor);
        QCOMPARE(disabled.cursor().shape(), Qt::ArrowCursor);
        QCOMPARE(ownCursor.cursor().shape(), Qt::CrossCursor);
    }

    void reenabledButtonShowsThePointingHandAgain()
    {
        QWidget window;
        QToolButton button(&window);
        window.ensurePolished();

        button.setEnabled(false);
        QCOMPARE(button.cursor().shape(), Qt::ArrowCursor);
        button.setEnabled(true);

        QCOMPARE(button.cursor().shape(), Qt::PointingHandCursor);
    }

    void tabBarShowsThePointingHandOnlyOverATab()
    {
        QWidget window;
        QTabBar tabBar(&window);
        tabBar.setExpanding(false); // room beside the tab
        tabBar.addTab(QStringLiteral("One"));
        tabBar.resize(400, 30);
        window.ensurePolished();
        const auto moveTo = [&tabBar](QPoint position) {
            QMouseEvent move(QEvent::MouseMove, position, tabBar.mapToGlobal(position), Qt::NoButton, Qt::NoButton,
                             Qt::NoModifier);
            QApplication::sendEvent(&tabBar, &move);
        };

        moveTo(tabBar.tabRect(0).center());
        QCOMPARE(tabBar.cursor().shape(), Qt::PointingHandCursor);
        moveTo({390, 15});
        QCOMPARE(tabBar.cursor().shape(), Qt::ArrowCursor);
    }

    void paletteMatchesLettersInOrder()
    {
        QVERIFY(CommandPalette::matchScore("Git: Stage All", "stall") > 0);
        QCOMPARE(CommandPalette::matchScore("Git: Stage All", "llats"), -1);
        QCOMPARE(CommandPalette::matchScore("View: Diff Mode", ""), 0);
        // Word starts and consecutive letters rank first.
        QVERIFY(CommandPalette::matchScore("File: Save All", "save") >
                CommandPalette::matchScore("Navigate: Go to Definition", "save"));
    }

    void longNamesScrollHorizontally()
    {
        const QTemporaryDir dir;
        const QString name = QString(120, QLatin1Char('n')) + QStringLiteral(".txt");
        QFile file(dir.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        FileTreeView tree;
        tree.resize(200, 300);
        tree.setRootPath(dir.path());
        tree.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tree));

        // The column widens to the longest name, without truncating it.
        QTRY_VERIFY(tree.header()->sectionSize(0) > tree.viewport()->width());
        QTRY_VERIFY(tree.horizontalScrollBar()->maximum() > 0);
    }

    void shortNamesFillTheView()
    {
        const QTemporaryDir dir;
        QFile file(dir.filePath(QStringLiteral("a.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        FileTreeView tree;
        tree.resize(400, 300);
        tree.setRootPath(dir.path());
        tree.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tree));

        QTRY_COMPARE(tree.header()->sectionSize(0), tree.viewport()->width());
        QCOMPARE(tree.horizontalScrollBar()->maximum(), 0);
    }

    void treeMetricsKeepCustomDelegates()
    {
        // A custom delegate stays (its view relies on it); a plain one gets
        // the row height.
        FileTreeView tree;
        const QAbstractItemDelegate *custom = tree.itemDelegate();
        applyTreeMetrics(&tree, QApplication::font());
        QVERIFY(tree.itemDelegate() == custom);
        QTreeView plain;
        const QAbstractItemDelegate *standard = plain.itemDelegate();
        applyTreeMetrics(&plain, QApplication::font());
        QVERIFY(plain.itemDelegate() != standard);
    }

    void titlesFollowTheInterfaceFont()
    {
        const QFont previous = QApplication::font();
        QFont font = previous;
        font.setPointSizeF(10);
        QApplication::setFont(font);
        QWidget window;
        auto *title = new QLabel("Title", &window);
        AppFonts::setInterfaceFont(title, 1.5);
        QCOMPARE(title->font().pointSizeF(), 15.0);
        QCOMPARE(title->font().weight(), QFont::DemiBold);
        font.setPointSizeF(12); // Preferences ▸ Fonts
        QApplication::setFont(font);
        QCOMPARE(title->font().pointSizeF(), 18.0);
        QApplication::setFont(previous);
    }

    void overlayPopupsAreCenteredAndResizable()
    {
        QSettings().remove("overlays/test");
        QSettings().remove("overlays/testOffset");
        QWidget window;
        window.resize(1000, 700);
        auto *button = new QPushButton("elsewhere", &window);
        button->setGeometry(5, 650, 120, 30);
        window.show();
        button->setFocus();
        OverlayPopup popup("test", QSizeF(0.5, 0.5), QSize(300, 200), &window);
        popup.showCentered();
        QVERIFY(popup.isVisible());
        const int shadow = OverlayPopup::shadowWidth;
        // Half of the room the window offers (its size less a margin).
        QCOMPARE(popup.size(), QSize((1000 - 48) / 2 + 2 * shadow, (700 - 48) / 2 + 2 * shadow));
        QCOMPARE(popup.geometry().center().x(), window.rect().center().x()); // centered across

        // Dragging the right edge widens it; the size is kept.
        const QPoint edge(popup.width() - 3, popup.height() / 2);
        QTest::mousePress(&popup, Qt::LeftButton, {}, edge);
        QMouseEvent move(QEvent::MouseMove, QPointF(edge + QPoint(100, 0)),
                         popup.mapToGlobal(QPointF(edge + QPoint(100, 0))), Qt::NoButton, Qt::LeftButton,
                         Qt::NoModifier);
        QApplication::sendEvent(&popup, &move);
        QTest::mouseRelease(&popup, Qt::LeftButton, {}, edge + QPoint(100, 0));
        QCOMPARE(popup.width(), (1000 - 48) / 2 + 2 * shadow + 100);
        const QSize chosen = QSettings().value("overlays/test").toSize();
        QCOMPARE(chosen, popup.size() - QSize(2 * shadow, 2 * shadow));

        QTest::mouseClick(button, Qt::LeftButton); // a click elsewhere leaves it open
        QVERIFY(popup.isVisible());
        popup.dismiss();
        popup.showCentered();
        QCOMPARE(popup.size(), chosen + QSize(2 * shadow, 2 * shadow));
        window.resize(500, 400); // too small: the panel shrinks to fit
        QVERIFY(popup.width() <= 500);
        popup.dismiss();
        QVERIFY(!popup.isVisible());
        popup.resetSize();
        QVERIFY(!QSettings().contains("overlays/test"));
        QVERIFY(!QSettings().contains("overlays/testOffset"));
    }

    void transientPopupsCloseOnAClickElsewhereAndReopenCentered()
    {
        QSettings().remove("overlays/transient");
        QSettings().remove("overlays/transientOffset");
        QWidget window;
        window.resize(1000, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        OverlayPopup popup("transient", QSizeF(0.5, 0.5), QSize(300, 200), &window);
        popup.setBehavior(OverlayPopup::Behavior::Transient);
        popup.showCentered();
        QCOMPARE(popup.geometry().center(), window.rect().center());

        // Moved, then a click inside: it stays open.
        popup.move(popup.pos() + QPoint(80, 40));
        QTest::mouseClick(window.windowHandle(), Qt::LeftButton, {}, popup.geometry().center());
        QVERIFY(popup.isVisible());

        QTest::mouseClick(window.windowHandle(), Qt::LeftButton, {}, QPoint(5, 5));
        QVERIFY(!popup.isVisible());

        popup.showCentered();
        QCOMPARE(popup.geometry().center(), window.rect().center());
        QVERIFY(!QSettings().contains("overlays/transientOffset"));
    }

    void popupFadesInAndHidesAtOnceBehindAFadingPicture()
    {
        QWidget window;
        window.resize(1000, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        OverlayPopup popup("fading", QSizeF(0.5, 0.5), QSize(300, 200), &window);

        popup.showCentered();
        QVERIFY(qobject_cast<QGraphicsOpacityEffect *>(popup.graphicsEffect()));
        QTRY_VERIFY(!popup.graphicsEffect());

        popup.dismiss();
        QVERIFY(!popup.isVisible());
        const auto pictures = [&window] { return window.findChildren<QLabel *>(Qt::FindDirectChildrenOnly); };
        QCOMPARE(pictures().size(), 1);
        QVERIFY(pictures().first()->testAttribute(Qt::WA_TransparentForMouseEvents));
        QTRY_VERIFY(pictures().isEmpty());
    }

    void shadowMarginsSurviveAThemeChange()
    {
        // A style sheet resets the margins of the widgets it polishes: the
        // contents would cover the shadow and overflow the panel.
        QMainWindow window;
        window.resize(800, 600);
        OverlayPopup popup("test", QSizeF(0.5, 0.5), QSize(300, 200), &window);
        auto *frame = new WindowFrame(&window);
        frame->setFrameless(true);
        window.show();
        ThemeStyle::apply(Theme::named("kodamiko-lagoon"));
        ThemeStyle::apply(Theme::named("kodamiko-slate"));
        QCoreApplication::processEvents();
        const int shadow = OverlayPopup::shadowWidth;
        QCOMPARE(popup.contentsMargins(), QMargins(shadow, shadow, shadow, shadow));
        popup.showCentered();
        QCOMPARE(popup.contentsMargins(), QMargins(shadow, shadow, shadow, shadow));
        const int frameShadow = WindowFrame::shadowWidth;
        QTRY_COMPARE(window.contentsMargins(), QMargins(frameShadow, frameShadow, frameShadow, frameShadow));
        qApp->setStyleSheet({});
        QSettings().remove("overlays/test");
    }

    void sidePanelFoldsLikeKDevelop()
    {
        QWidget window;
        auto *layout = new QHBoxLayout(&window);
        auto *panel = new SidePanel(&window);
        auto *files = new QWidget;
        auto *outline = new QWidget;
        panel->addView(files, {}, "Files");
        panel->addView(outline, {}, "Outline");
        layout->addWidget(panel->strip());
        layout->addWidget(panel, 1);
        window.resize(600, 400);
        window.show();
        const QSignalSpy visibility(panel, &SidePanel::panelVisibilityChanged);
        QTabBar *tabs = panel->tabBar();
        QCOMPARE(tabs->shape(), QTabBar::RoundedWest); // vertical tabs
        QVERIFY(panel->isPanelVisible());

        // The current tab, clicked: the panel folds away; clicked again: back.
        QTest::mouseClick(tabs, Qt::LeftButton, {}, tabs->tabRect(0).center());
        QVERIFY(!panel->isPanelVisible());
        QVERIFY(panel->strip()->isVisible()); // the strip stays
        QTest::mouseClick(tabs, Qt::LeftButton, {}, tabs->tabRect(0).center());
        QVERIFY(panel->isPanelVisible());
        // Another tab: shows its view.
        QTest::mouseClick(tabs, Qt::LeftButton, {}, tabs->tabRect(1).center());
        QCOMPARE(panel->currentWidget(), outline);
        panel->setPanelVisible(false);
        panel->showView(files); // Ctrl+Shift+O, Alt+F1: unfolds it
        QVERIFY(panel->isPanelVisible());
        QCOMPARE(tabs->currentIndex(), 0);
        QCOMPARE(visibility.size(), 4);
    }

    void frameResizesFromItsEdges()
    {
        const QSize size(800, 600);
        const int margin = WindowFrame::shadowWidth;
        QCOMPARE(WindowFrame::edgesAt({400, 300}, size, margin), Qt::Edges());
        QCOMPARE(WindowFrame::edgesAt({2, 300}, size, margin), Qt::Edges(Qt::LeftEdge));
        QCOMPARE(WindowFrame::edgesAt({798, 300}, size, margin), Qt::Edges(Qt::RightEdge));
        QCOMPARE(WindowFrame::edgesAt({400, 1}, size, margin), Qt::Edges(Qt::TopEdge));
        QCOMPARE(WindowFrame::edgesAt({400, 599}, size, margin), Qt::Edges(Qt::BottomEdge));
        QCOMPARE(WindowFrame::edgesAt({3, 3}, size, margin), Qt::TopEdge | Qt::LeftEdge);
        QCOMPARE(WindowFrame::edgesAt({795, 595}, size, margin), Qt::BottomEdge | Qt::RightEdge);
        // A little inside the window, the corners still resize; the sides do not.
        QCOMPARE(WindowFrame::edgesAt({margin + 2, margin + 2}, size, margin), Qt::TopEdge | Qt::LeftEdge);
        QCOMPARE(WindowFrame::edgesAt({margin + 2, 300}, size, margin), Qt::Edges());
    }

    void projectChipShowsTheFilesTreeFolder()
    {
        TitleBar bar;
        const auto *chip = bar.findChild<QToolButton *>("projectChip");

        bar.applyTheme(Theme::kodamiko());
        QCOMPARE(chip->icon().cacheKey(), FileIcons::folder(true).cacheKey());
    }

    void titleBarOverflowRemainsVisibleAndOpensHiddenMenus_data()
    {
        QTest::addColumn<QString>("themeName");
        QTest::addColumn<int>("fontSize");
        QTest::newRow("kodamiko") << QStringLiteral("kodamiko") << 11;
        QTest::newRow("kodamiko-slate") << QStringLiteral("kodamiko-slate") << 11;
        QTest::newRow("kodamiko-large-font") << QStringLiteral("kodamiko") << 15;
        QTest::newRow("kodamiko-slate-large-font") << QStringLiteral("kodamiko-slate") << 15;
    }

    void titleBarOverflowRemainsVisibleAndOpensHiddenMenus()
    {
        QFETCH(const QString, themeName);
        QFETCH(const int, fontSize);
        const QFont previousFont = QApplication::font();
        const QPalette previousPalette = QApplication::palette();
        const QString previousStyle = qApp->styleSheet();
        const auto restore = qScopeGuard([&] {
            qApp->setStyleSheet(previousStyle);
            QApplication::setPalette(previousPalette);
            QApplication::setFont(previousFont);
        });
        QFont font = previousFont;
        font.setPointSize(fontSize);
        QApplication::setFont(font);
        const Theme theme = Theme::named(themeName);
        ThemeStyle::apply(theme);
        TitleBar bar;
        bar.applyTheme(theme);
        for (const auto *name : {"&File", "&Edit", "&View", "&Navigate", "&Claude", "&Git", "&Run", "&Help"})
            bar.menuBar()->addMenu(name)->addAction("Action");
        bar.setProject("kodamiko", "/p/kodamiko", nullptr);
        bar.resize(400, bar.height());
        bar.show();

        auto *extension = bar.findChild<QToolButton *>("mainMenuButton");
        QVERIFY(extension);
        QTRY_VERIFY(extension->isVisible());
        QCOMPARE(bar.childAt(extension->mapTo(&bar, extension->rect().center())), extension);
        // Visibility alone is insufficient: padding used to leave a blank button.
        const QImage image = extension->grab().toImage();
        int visiblePixels = 0;
        for (int y = image.height() / 4; y < 3 * image.height() / 4; ++y)
            for (int x = 1; x < image.width() - 1; ++x)
                if (qAbs(image.pixelColor(x, y).lightness() - theme.window.lightness()) > 40)
                    ++visiblePixels;
        QVERIFY2(visiblePixels >= 6, "The menu overflow arrow must be visible against the title bar");

        const QMenu *overflow = extension->menu();
        QVERIFY(overflow);
        const QSignalSpy opened(overflow, &QMenu::aboutToShow);
        QTimer::singleShot(0, overflow, &QMenu::close);
        QTest::mouseClick(extension, Qt::LeftButton);
        QCOMPARE(opened.count(), 1);
        QVERIFY(overflow->actions().contains(bar.menuBar()->actions().last()));

        bar.resize(1600, bar.height());
        QTRY_VERIFY(!extension->isVisible());
        bar.resize(400, bar.height());
        QTRY_VERIFY(extension->isVisible());
    }

    void agentIndicatorKeepsItsSizeAcrossStates()
    {
        AgentIndicator indicator;
        const QSize readySize = indicator.sizeHint();

        indicator.setStatus(AgentStatus::NeedsYou);

        QCOMPARE(indicator.sizeHint(), readySize);
    }

    void agentIndicatorTakesItsColorsFromTheTheme()
    {
        for (const Theme &theme :
             {Theme::kodamiko(), Theme::named("kodamiko-lagoon"), Theme::named("kodamiko-cocoa")}) {
            QCOMPARE(agentStatusColor(AgentStatus::Working, theme), theme.warning);
            QCOMPARE(agentStatusColor(AgentStatus::NeedsYou, theme), theme.accent);
            QCOMPARE(agentStatusColor(AgentStatus::Ready, theme), theme.mutedText);
        }
    }

    void frameAndTitleBarFollowTheWindow()
    {
        QMainWindow window;
        auto *titleBar = new TitleBar(&window);
        window.setMenuWidget(titleBar);
        auto *frame = new WindowFrame(&window);
        frame->setFrameless(true);
        window.resize(600, 400);
        window.show();
        QVERIFY(window.windowFlags() & Qt::FramelessWindowHint);
        QCOMPARE(window.contentsMargins(), QMargins(12, 12, 12, 12)); // the shadow

        // The menus live in the title bar.
        titleBar->menuBar()->addMenu("&File");
        QCOMPARE(titleBar->menuBar()->actions().size(), 1);

        auto *project = new QMenu(&window);
        project->addAction("other");
        titleBar->setProject("kodamiko", "/p/kodamiko", project);
        QStringList texts;
        for (const QToolButton *button : titleBar->findChildren<QToolButton *>())
            if (button->isVisibleTo(titleBar))
                texts.append(button->text());
        QVERIFY(texts.contains("kodamiko"));
        QVERIFY(!titleBar->findChild<AgentIndicator *>());

        QAction action("Go to File");
        int triggered = 0;
        connect(&action, &QAction::triggered, [&triggered] { ++triggered; });
        titleBar->addActionButton(&action, "search");
        titleBar->findChildren<QToolButton *>().last()->click();
        QCOMPARE(triggered, 1);

        // Maximizing drops the shadow; the system's frame drops it too.
        titleBar->windowButton(WindowButton::Kind::Maximize)->click();
        QTRY_VERIFY(window.isMaximized());
        QCOMPARE(window.contentsMargins(), QMargins());
        titleBar->windowButton(WindowButton::Kind::Maximize)->click();
        QTRY_VERIFY(!window.isMaximized());
        frame->setFrameless(false);
        QVERIFY(!(window.windowFlags() & Qt::FramelessWindowHint));
        QCOMPARE(window.contentsMargins(), QMargins());
        titleBar->setWindowControlsVisible(false);
        QVERIFY(!titleBar->windowButton(WindowButton::Kind::Close)->isVisibleTo(titleBar));
    }

    void modelSortsCompactsAndNests()
    {
        const QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.filePath("project"));
        write(root + "/src/main/java/App.java");
        write(root + "/src/main/java/Util.java");
        write(root + "/b.cpp");
        write(root + "/b.h");
        write(root + "/a.ts");
        write(root + "/a.js");
        write(root + "/a.js.map");
        write(root + "/a.d.ts");
        write(root + "/package.json");
        write(root + "/package-lock.json");
        write(root + "/Zeta.txt");
        write(root + "/orphan.h");
        write(root + "/.git/HEAD");
        QVERIFY(QDir().mkpath(root + "/docs"));

        ProjectTreeModel model;
        model.setRootPath(root);
        const QModelIndex top = model.index(0, 0);
        QCOMPARE(top.data().toString(), QString("project"));
        // Folders first, case ignored, .git left out; companions under their main file.
        QCOMPARE(rowsOf(model, top), QStringList({"docs", "src/main/java", "a.ts(a.d.ts,a.js,a.js.map)", "b.cpp(b.h)",
                                                  "orphan.h", "package.json(package-lock.json)", "Zeta.txt"}));
        // A folder inside the chain leads to the chain's row.
        QCOMPARE(model.filePath(model.index(root + "/src/main")), root + "/src/main/java");
        QCOMPARE(model.filePath(model.index(root + "/b.h").parent()), root + "/b.cpp");
        QCOMPARE(model.filePath(model.index(root + "/src/main/java/App.java")), root + "/src/main/java/App.java");
        QVERIFY(!model.index("/elsewhere").isValid());

        ProjectTreeModel::Options options;
        options.compactFolders = false;
        options.nestFiles = false;
        model.setOptions(options);
        QCOMPARE(rowsOf(model, model.index(0, 0)).mid(0, 4), QStringList({"docs", "src", "a.d.ts", "a.js"}));
    }

    void modelHidesIgnoredFiles()
    {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("git unavailable");
        const QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.filePath("project"));
        write(root + "/build/out.o");
        write(root + "/main.cpp");
        write(root + "/debug.log");
        QFile ignore(root + "/.gitignore");
        QVERIFY(ignore.open(QIODevice::WriteOnly));
        ignore.write("build/\n*.log\n");
        ignore.close();
        Git::run(root, {"init", "-q"});

        ProjectTreeModel model;
        ProjectTreeModel::Options options;
        options.showIgnored = false;
        model.setOptions(options);
        model.setRootPath(root);
        QCOMPARE(rowsOf(model, model.index(0, 0)), QStringList({".gitignore", "main.cpp"}));
    }

    void modelFollowsTheDiskKeepingRows()
    {
        const QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.filePath("project"));
        write(root + "/src/a.cpp");
        write(root + "/src/b.cpp");
        ProjectTreeModel model;
        model.setRootPath(root);
        const QPersistentModelIndex src = model.index(root + "/src");
        QCOMPARE(rowsOf(model, src), QStringList({"a.cpp", "b.cpp"}));

        write(root + "/src/a.h"); // nests under a.cpp
        QFile::remove(root + "/src/b.cpp");
        model.refresh();
        QVERIFY(src.isValid()); // the folder's row stayed
        QCOMPARE(rowsOf(model, src), QStringList({"a.cpp(a.h)"}));

        // A file next to the only folder of a folder splits a compact chain.
        write(root + "/one/two/x.txt");
        model.refresh();
        QVERIFY(rowsOf(model, model.index(0, 0)).contains("one/two"));
        write(root + "/one/y.txt");
        model.refresh();
        QCOMPARE(rowsOf(model, model.index(0, 0)), QStringList({"one", "src"}));
        // The watcher sees changes made elsewhere.
        write(root + "/src/c.cpp");
        QTRY_COMPARE(rowsOf(model, src), QStringList({"a.cpp(a.h)", "c.cpp"}));
    }

    // Many rows added and removed at once, in runs: the persistent rows
    // that stay keep pointing at the same files.
    void modelUpdatesRunsOfRows()
    {
        const QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.filePath("project"));
        for (int i = 0; i < 100; i += 2)
            write(root + QStringLiteral("/f%1.txt").arg(i, 3, 10, QLatin1Char('0')));
        ProjectTreeModel model;
        model.setRootPath(root);
        const QModelIndex top = model.index(0, 0);
        QCOMPARE(rowsOf(model, top).size(), 50);
        const QPersistentModelIndex kept = model.index(30, 0, top); // f060.txt
        QCOMPARE(kept.data().toString(), QString("f060.txt"));

        for (int i = 1; i < 100; i += 2)
            write(root + QStringLiteral("/f%1.txt").arg(i, 3, 10, QLatin1Char('0')));
        for (int i = 0; i < 40; i += 2)
            QFile::remove(root + QStringLiteral("/f%1.txt").arg(i, 3, 10, QLatin1Char('0')));
        model.refresh();

        QStringList expected;
        for (int i = 0; i < 100; ++i) {
            if (i >= 40 || i % 2 == 1)
                expected.append(QStringLiteral("f%1.txt").arg(i, 3, 10, QLatin1Char('0')));
        }
        QCOMPARE(rowsOf(model, top), expected);
        QVERIFY(kept.isValid());
        QCOMPARE(kept.data().toString(), QString("f060.txt"));
        QCOMPARE(kept.row(), int(expected.indexOf("f060.txt")));
    }

    void nestingRules()
    {
        const QStringList siblings{"x.ts", "x.js", "x.js.map", "x.d.ts", "y.c", "y.h", "z.h", "go.mod", "go.sum"};
        QCOMPARE(ProjectTreeModel::nestingParent("x.js", siblings), QString("x.ts"));
        QCOMPARE(ProjectTreeModel::nestingParent("x.js.map", siblings), QString("x.ts")); // up the chain
        QCOMPARE(ProjectTreeModel::nestingParent("x.d.ts", siblings), QString("x.ts"));
        QCOMPARE(ProjectTreeModel::nestingParent("x.ts", siblings), QString());
        QCOMPARE(ProjectTreeModel::nestingParent("y.h", siblings), QString("y.c"));
        QCOMPARE(ProjectTreeModel::nestingParent("z.h", siblings), QString());
        QCOMPARE(ProjectTreeModel::nestingParent("go.sum", siblings), QString("go.mod"));
    }

    void fileIconsByKind()
    {
        QCOMPARE(FileIcons::kindOf("Main.CPP"), QString("cpp"));
        QCOMPARE(FileIcons::kindOf("CMakeLists.txt"), QString("cmake"));
        QCOMPARE(FileIcons::kindOf("x.d.ts"), QString("typescript"));
        QCOMPARE(FileIcons::kindOf("photo.jpeg"), QString("image"));
        QCOMPARE(FileIcons::kindOf("README"), QString("text"));
        QVERIFY(!FileIcons::file("a.cpp", true).isNull());
        QVERIFY(!FileIcons::folder(false).isNull());
    }

    void fileOperationsNeverOverwrite()
    {
        const QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.path());
        write(root + "/src/a.cpp");
        write(root + "/src/deep/b.cpp");
        write(root + "/other/a.cpp");

        QVERIFY(FileOperations::copy(root + "/src", root + "/copy").isEmpty());
        QVERIFY(QFileInfo::exists(root + "/copy/deep/b.cpp"));
        QVERIFY(!FileOperations::copy(root + "/src/a.cpp", root + "/other/a.cpp").isEmpty());
        QVERIFY(!FileOperations::copy(root + "/src", root + "/src/deep/inside").isEmpty());
        QVERIFY(!QFileInfo::exists(root + "/src/deep/inside"));
        QVERIFY(!FileOperations::move(root + "/src", root + "/src/deep/x").isEmpty());
        QVERIFY(FileOperations::move(root + "/copy", root + "/moved").isEmpty());
        QVERIFY(!QFileInfo::exists(root + "/copy") && QFileInfo::exists(root + "/moved/a.cpp"));

        QCOMPARE(FileOperations::copyName(root + "/src", "a.cpp"), QString("a_copy.cpp"));
        write(root + "/src/a_copy.cpp");
        QCOMPARE(FileOperations::copyName(root + "/src", "a.cpp"), QString("a_copy2.cpp"));
        QCOMPARE(FileOperations::freeName(root + "/src", "new.cpp"), QString("new.cpp"));
        QCOMPARE(FileOperations::freeName(root + "/src", "a.cpp"), QString("a (2).cpp"));
        QCOMPARE(FileOperations::copyName(root, ".gitignore"), QString(".gitignore_copy"));
    }

    void treeCutCopyAndPaste()
    {
        const QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.filePath("project"));
        write(root + "/src/a.cpp");
        write(root + "/lib/keep.txt");
        FileTreeView tree;
        tree.setRootPath(root);
        tree.show();
        QSignalSpy renamed(&tree, &FileTreeView::entryRenamed);
        const QSignalSpy created(&tree, &FileTreeView::entryCreated);

        // Copy, then paste into another folder.
        QVERIFY(tree.select(root + "/src/a.cpp"));
        QTest::keyClick(&tree, Qt::Key_C, Qt::ControlModifier);
        QVERIFY(tree.select(root + "/lib"));
        QTest::keyClick(&tree, Qt::Key_V, Qt::ControlModifier);
        QVERIFY(QFileInfo::exists(root + "/lib/a.cpp"));
        QVERIFY(QFileInfo::exists(root + "/src/a.cpp"));
        QCOMPARE(created.size(), 1);

        // Cut, then paste: a move the editor tabs can follow.
        QVERIFY(tree.select(root + "/src/a.cpp"));
        QTest::keyClick(&tree, Qt::Key_X, Qt::ControlModifier);
        QVERIFY(tree.select(root + "/lib/keep.txt"));           // a file: its folder
        QTest::keyClick(&tree, Qt::Key_V, Qt::ControlModifier); // name taken: a popup asks
        const QPointer<NamePopup> popup = tree.findChild<NamePopup *>();
        QVERIFY(popup);
        QCOMPARE(popup->field()->text(), QString("a (2).cpp"));
        QTest::keyClick(popup->field(), Qt::Key_Return);
        QVERIFY(QFileInfo::exists(root + "/lib/a (2).cpp"));
        QVERIFY(!QFileInfo::exists(root + "/src/a.cpp"));
        QCOMPARE(renamed.size(), 1);
        QCOMPARE(renamed.first().at(1).toString(), root + "/lib/a (2).cpp");
        QTRY_COMPARE(tree.currentPath(), root + "/lib/a (2).cpp");
        // A cut is pasted once.
        const QMimeData *clipboard = QApplication::clipboard()->mimeData();
        QVERIFY(!clipboard || !clipboard->hasFormat("application/x-kodamiko-cut"));
    }

    void treeDuplicatesUnderANewName()
    {
        const QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.filePath("project"));
        write(root + "/Widget.cpp");
        FileTreeView tree;
        tree.setRootPath(root);
        tree.show();
        QVERIFY(tree.select(root + "/Widget.cpp"));
        QTest::keyClick(&tree, Qt::Key_F5);
        const QPointer<NamePopup> popup = tree.findChild<NamePopup *>();
        QVERIFY(popup);
        QCOMPARE(popup->field()->text(), QString("Widget_copy.cpp"));
        QCOMPARE(popup->field()->selectedText(), QString("Widget_copy"));
        QTest::keyClick(popup->field(), Qt::Key_Return);
        QVERIFY(QFileInfo::exists(root + "/Widget_copy.cpp"));
    }

    void treeColorsFilesByGitState()
    {
        const QTemporaryDir dir;
        write(dir.filePath("src/a.cpp"));
        write(dir.filePath("b.txt"));
        const QString root = QDir::cleanPath(dir.path());
        FileTreeView tree;
        tree.setRootPath(root);
        tree.setStatuses({{root + "/src/a.cpp", FileStatus::Modified}, {root + "/b.txt", FileStatus::Untracked}}, true);
        tree.setAgentName(QStringLiteral("Claude"));
        tree.setClaudeTouched({root + "/b.txt"});
        tree.show();
        QVERIFY(tree.select(root + "/src/a.cpp"));
        QTRY_COMPARE(tree.currentPath(), root + "/src/a.cpp");

        const QModelIndex file = tree.currentIndex();
        const QModelIndex src = file.parent();
        const auto modified = file.data(Qt::ForegroundRole).value<QColor>();
        QVERIFY(modified.isValid());
        QCOMPARE(src.data(Qt::ForegroundRole).value<QColor>(), modified); // the folder that contains it
        QVERIFY(src.data(Qt::ToolTipRole).toString().contains("contains changes"));
        QVERIFY(tree.select(root + "/b.txt"));
        QTRY_COMPARE(tree.currentPath(), root + "/b.txt");
        QVERIFY(tree.currentIndex().data(Qt::ForegroundRole).value<QColor>() != modified);
        QVERIFY(tree.currentIndex().data(Qt::ToolTipRole).toString().contains("just edited by Claude"));
        QVERIFY(!tree.select("/elsewhere/x"));
    }

    void scopesShowOnlyTheirFiles()
    {
        const QTemporaryDir dir;
        write(dir.filePath("src/deep/changed.cpp"));
        write(dir.filePath("src/same.cpp"));
        write(dir.filePath("other/claude.md"));
        const QString root = QDir::cleanPath(dir.path());
        FilesPanel panel;
        FileTreeView *tree = panel.tree();
        tree->setRootPath(root);
        tree->setStatuses({{root + "/src/deep/changed.cpp", FileStatus::Modified}}, true);
        tree->setClaudeEdited({root + "/other/claude.md"});
        panel.show();
        tree->restoreExpandedPaths({root + "/src"});

        const auto shown = [tree] {
            QStringList paths;
            for (QModelIndex index = tree->model()->index(0, 0, tree->rootIndex()); index.isValid();
                 index = tree->indexBelow(index))
                paths.append(QFileInfo(index.data(Qt::UserRole + 100).toString()).fileName());
            return paths;
        };
        QTRY_VERIFY(shown().contains("same.cpp"));

        panel.setScopeIndex(1); // Changed Files: everything expanded
        QTRY_COMPARE(shown(), QStringList({QFileInfo(root).fileName(), "src", "deep", "changed.cpp"}));
        QVERIFY(!tree->select(root + "/src/same.cpp"));
        panel.setScopeIndex(2);
        QTRY_COMPARE(shown(), QStringList({QFileInfo(root).fileName(), "other", "claude.md"}));
        panel.setScopeIndex(0); // back to the project, as it was
        QTRY_VERIFY(shown().contains("same.cpp"));
        QCOMPARE(tree->expandedPaths(), QStringList({root, root + "/src"}));
    }

    void speedSearchSelectsMatchingNames()
    {
        const QTemporaryDir dir;
        // A root whose name matches none of the letters typed.
        const QString root = QDir::cleanPath(dir.filePath("project"));
        write(root + "/alpha.cpp");
        write(root + "/beta.cpp");
        write(root + "/bravo.h");
        FileTreeView tree;
        tree.setRootPath(root);
        tree.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tree));
        QTRY_VERIFY(tree.select(root + "/alpha.cpp") && tree.currentPath() == root + "/alpha.cpp");

        // Once the folder has been read entirely.
        QTRY_COMPARE(tree.model()->rowCount(tree.currentIndex().parent()), 3);
        QTest::keyClicks(&tree, "bh");
        QCOMPARE(tree.speedSearch(), QString("bh"));
        QCOMPARE(tree.currentPath(), root + "/bravo.h");
        QTest::keyClick(&tree, Qt::Key_Backspace);
        QCOMPARE(tree.currentPath(), root + "/bravo.h"); // still matches "b"
        // Up and Down go to the other match of "b", skipping alpha.cpp.
        QTest::keyClick(&tree, Qt::Key_Up);
        if (tree.currentPath() != root + "/beta.cpp")
            QTest::keyClick(&tree, Qt::Key_Down);
        QCOMPARE(tree.currentPath(), root + "/beta.cpp");
        QTest::keyClick(&tree, Qt::Key_Escape);
        QVERIFY(tree.speedSearch().isEmpty());

        const QSignalSpy activated(&tree, &FileTreeView::fileActivated);
        QTest::keyClicks(&tree, "alp");
        QCOMPARE(tree.currentPath(), root + "/alpha.cpp");
        QTest::keyClick(&tree, Qt::Key_Return);
        QCOMPARE(activated.size(), 1);
        QVERIFY(tree.speedSearch().isEmpty());
    }

    void treeCopiesPathsAndSendsToClaude()
    {
        const QTemporaryDir dir;
        write(dir.filePath("src/a.cpp"));
        const QString root = QDir::cleanPath(dir.path());
        FileTreeView tree;
        tree.setRootPath(root);
        tree.show();
        QTRY_VERIFY(tree.select(root + "/src/a.cpp") && tree.currentPath() == root + "/src/a.cpp");
        QTest::keyClick(&tree, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(QApplication::clipboard()->text(), QDir::toNativeSeparators(root + "/src/a.cpp"));
        QTest::keyClick(&tree, Qt::Key_C, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
        QCOMPARE(QApplication::clipboard()->text(), QDir::toNativeSeparators("src/a.cpp"));
        QSignalSpy send(&tree, &FileTreeView::sendToClaudeRequested);
        QTest::keyClick(&tree, Qt::Key_K, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(send.size(), 1);
        QCOMPARE(send.first().at(0).toString(), root + "/src/a.cpp");
    }

    void newEntryNamesAreChecked()
    {
        const QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("src"));
        QFile file(dir.filePath("src/a.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const QString root = dir.path();
        const QString src = dir.filePath("src");

        QCOMPARE(resolveNewEntry(root, src, "b.txt", false).path, dir.filePath("src/b.txt"));
        QCOMPARE(resolveNewEntry(root, src, " new/deep/c.txt ", false).path, dir.filePath("src/new/deep/c.txt"));
        QCOMPARE(resolveNewEntry(root, src, "new/", true).path, dir.filePath("src/new"));

        QCOMPARE(resolveNewEntry(root, src, "  ", false).error, QString("Specify a file name"));
        QCOMPARE(resolveNewEntry(root, src, "dir/", false).error, QString("Specify a file name"));
        QCOMPARE(resolveNewEntry(root, src, "a.txt", false).error,
                 QString("A file with the name \u201ca.txt\u201d already exists"));
        QCOMPARE(resolveNewEntry(root, root, "src", true).error,
                 QString("A folder with the name \u201csrc\u201d already exists"));
        // An existing file cannot become a folder.
        QVERIFY(resolveNewEntry(root, src, "a.txt/b.txt", false).error.contains("already exists"));
        QVERIFY(resolveNewEntry(root, src, "../../outside.txt", false).error.contains("invalid"));
        QVERIFY(resolveNewEntry(root, src, "/etc/x", false).error.contains("invalid"));
    }

    void newEntryPopupShowsErrorsUntilEdited()
    {
        QWidget window;
        window.resize(600, 400);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QStringList applied;
        const QPointer<NamePopup> popup = new NamePopup(
            "New File", "project/",
            [&applied](const QString &name) {
                applied << name;
                return name == "taken" ? QString("A file with the name \u201ctaken\u201d already exists") : QString();
            },
            &window);
        popup->popup();
        QVERIFY(QTest::qWaitForWindowExposed(popup));

        QTest::keyClicks(popup->field(), "taken");
        QTest::keyClick(popup->field(), Qt::Key_Return);
        QVERIFY(popup);
        QVERIFY(popup->error().contains("already exists"));
        QTest::keyClick(popup->field(), Qt::Key_Backspace);
        QVERIFY(popup->error().isEmpty());

        QTest::keyClicks(popup->field(), "x");
        QTest::keyClick(popup->field(), Qt::Key_Return);
        QCOMPARE(applied, QStringList({"taken", "takex"}));
        QTRY_VERIFY(!popup); // closed and deleted
    }

    void newEntryPopupClosesOnEscape()
    {
        QWidget window;
        window.resize(600, 400);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        bool applied = false;
        const QPointer<NamePopup> popup = new NamePopup(
            "New Folder", "project/",
            [&applied](const QString &) {
                applied = true;
                return QString();
            },
            &window);
        popup->popup();
        QVERIFY(QTest::qWaitForWindowExposed(popup));
        QTest::keyClick(popup->field(), Qt::Key_Escape);
        QTRY_VERIFY(!popup);
        QVERIFY(!applied);
    }

    void renameNamesAreChecked()
    {
        const QTemporaryDir dir;
        for (const char *name : {"a.txt", "b.txt"}) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        const QString a = dir.filePath("a.txt");
        QCOMPARE(resolveRename(a, "c.txt").path, dir.filePath("c.txt"));
        QCOMPARE(resolveRename(a, " a.txt ").path, a); // unchanged
        QCOMPARE(resolveRename(a, "A.txt").path, dir.filePath("A.txt"));
        QCOMPARE(resolveRename(a, "").error, QString("Specify a new name"));
        QCOMPARE(resolveRename(a, "b.txt").error, QString("A file with the name “b.txt” already exists"));
        QVERIFY(resolveRename(a, "sub/c.txt").error.contains("invalid"));
        QVERIFY(resolveRename(a, "..").error.contains("invalid"));
    }

    void renameSelectsTheBaseName()
    {
        QWidget window;
        auto *popup = new NamePopup("Rename File", "project/", [](const QString &) { return QString(); }, &window);
        popup->setName("archive.tar.gz", false);
        QCOMPARE(popup->field()->selectedText(), QString("archive.tar"));
        popup->setName(".gitignore", false);
        QCOMPARE(popup->field()->selectedText(), QString(".gitignore"));
        popup->setName("v1.2", true);
        QCOMPARE(popup->field()->selectedText(), QString("v1.2"));
        delete popup;
    }

    void confirmPopupAppliesOnEnterAndCancelsFromCancel()
    {
        QWidget window;
        window.resize(600, 400);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        int calls = 0;
        QPointer<ConfirmPopup> popup = new ConfirmPopup(
            {"Delete File", "project/", "Delete “a.txt”?", "Delete"},
            [&calls](ConfirmPopup *asking) {
                // First call: asks something else, as when the trash fails.
                if (++calls == 1) {
                    asking->setMessage("Delete it permanently?", "Delete Permanently");
                    return false;
                }
                return true;
            },
            &window);
        popup->popup();
        QVERIFY(QTest::qWaitForWindowExposed(popup));
        QCOMPARE(popup->focusWidget(), popup->actionButton());

        QTest::keyClick(popup->actionButton(), Qt::Key_Return);
        QVERIFY(popup);
        QCOMPARE(popup->actionButton()->text(), QString("Delete Permanently"));
        QTest::keyClick(popup->actionButton(), Qt::Key_Return);
        QCOMPARE(calls, 2);
        QTRY_VERIFY(!popup);

        bool applied = false;
        popup = new ConfirmPopup(
            {"Delete File", "project/", "Delete?", "Delete"}, [&applied](ConfirmPopup *) { return applied = true; },
            &window);
        popup->popup();
        QVERIFY(QTest::qWaitForWindowExposed(popup));
        popup->cancelButton()->setFocus();
        QTest::keyClick(popup->cancelButton(), Qt::Key_Return);
        QTRY_VERIFY(!popup);
        QVERIFY(!applied);
    }

    void choicePopupRunsTheFocusedChoiceOnEnter()
    {
        QWidget window;
        window.resize(600, 400);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QString chosen;
        const QPointer<ChoicePopup> popup = new ChoicePopup(
            {.title = "Pull", .location = {}, .message = "Diverged", .details = "Rebase or merge"},
            {{"Rebase", [&chosen] { chosen = "rebase"; }}, {"Merge", [&chosen] { chosen = "merge"; }}}, &window);
        popup->popup();
        QVERIFY(QTest::qWaitForWindowExposed(popup));
        QCOMPARE(popup->focusWidget(), popup->choiceButtons().first());

        popup->choiceButtons().at(1)->setFocus();
        QTest::keyClick(popup->choiceButtons().at(1), Qt::Key_Return);

        QCOMPARE(chosen, QString("merge"));
        QTRY_VERIFY(!popup);
    }

    void choicePopupWaitsForTheAnswerAndCancelsOnEscape()
    {
        QWidget window;
        window.resize(600, 400);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        bool discarded = false;
        auto *popup =
            new ChoicePopup({.title = "Discard Changes", .location = {}, .message = "Discard?", .details = {}},
                            {{"Discard", [&discarded] { discarded = true; }}}, &window);
        popup->focusCancel();
        QTimer::singleShot(0, &window, [&window] {
            auto *shown = window.findChild<ChoicePopup *>();
            QVERIFY(shown);
            QCOMPARE(shown->focusWidget(), shown->cancelButton());
            QTest::keyClick(shown, Qt::Key_Escape);
        });
        popup->exec();

        QVERIFY(!discarded);
        QVERIFY(!window.findChild<ChoicePopup *>());
    }

    void noticeOnlyOffersToCloseIt()
    {
        QWidget window;
        window.resize(600, 400);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        showNotice({.title = "Cannot Save File", .location = "a.txt", .message = "Disk full", .details = {}}, &window);

        const QPointer<ChoicePopup> notice = window.findChild<ChoicePopup *>();
        QVERIFY(notice);
        QVERIFY(notice->choiceButtons().isEmpty());
        QCOMPARE(notice->cancelButton()->text(), QString("Close"));
        QTest::keyClick(notice->cancelButton(), Qt::Key_Return);
        QTRY_VERIFY(!notice);
    }

    void tabsFollowARenamedFolder()
    {
        const QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("src"));
        for (const char *name : {"src/a.txt", "b.txt"}) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("one\ntwo\nthree\n");
        }
        EditorTabs tabs(Theme::kodamiko());
        QVERIFY(tabs.openFile(dir.filePath("src/a.txt")));
        QVERIFY(tabs.openFile(dir.filePath("b.txt")));
        tabs.editorFor(dir.filePath("src/a.txt"))->setCursorPosition(2, 1);

        QVERIFY(!tabs.hasUnsavedChangesUnder(dir.filePath("src")));
        tabs.editorFor(dir.filePath("b.txt"))->insert("x");
        QVERIFY(tabs.hasUnsavedChangesUnder(dir.path()));
        QVERIFY(!tabs.hasUnsavedChangesUnder(dir.filePath("src")));
        // A prefix of the name is not a parent folder.
        QVERIFY(!tabs.hasUnsavedChangesUnder(dir.filePath("b.tx")));

        QVERIFY(QDir().rename(dir.filePath("src"), dir.filePath("lib")));
        tabs.followRename(dir.filePath("src"), dir.filePath("lib"));
        QCOMPARE(tabs.openFilePaths(), QStringList({dir.filePath("lib/a.txt"), dir.filePath("b.txt")}));
        int line = 0;
        int column = 0;
        tabs.editorFor(dir.filePath("lib/a.txt"))->getCursorPosition(&line, &column);
        QCOMPARE(line, 2);
        QCOMPARE(column, 1);
        QCOMPARE(tabs.currentFilePath(), dir.filePath("b.txt"));

        tabs.closeFilesUnder(dir.filePath("b.txt")); // unsaved changes discarded, no question
        QCOMPARE(tabs.openFilePaths(), QStringList({dir.filePath("lib/a.txt")}));
    }

    void trailingLayoutSetsGroupsApart()
    {
        QWidget row;
        auto *layout = new TrailingLayout(&row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        layout->setGroupSpacing(16);
        const auto add = [&row, layout] {
            auto *widget = new QWidget(&row);
            widget->setFixedSize(10, 10);
            layout->addWidget(widget);
            return widget;
        };
        layout->startGroup();
        const QWidget *anchor = add();
        layout->startGroup();
        const QWidget *first = add();
        const QWidget *second = add();
        layout->startGroup();
        QWidget *hidden = add();
        hidden->hide();
        layout->startGroup();
        const QWidget *last = add();
        row.resize(200, 10);
        row.show();
        QVERIFY(QTest::qWaitForWindowExposed(&row));

        QCOMPARE(anchor->geometry().right(), 199);
        QCOMPARE(anchor->x() - first->geometry().right() - 1, 16); // another group
        QCOMPARE(first->x() - second->geometry().right() - 1, 4);  // the same group
        // An empty group adds no space.
        QCOMPARE(second->x() - last->geometry().right() - 1, 16);
        QCOMPARE(layout->sizeHint().width(), 4 * 10 + 16 + 4 + 16);
    }

    void closeButtonsShowOnTheCurrentAndHoveredTabs()
    {
        QTabWidget tabs;
        tabs.setTabsClosable(true);
        for (const char *title : {"one", "two", "three"})
            tabs.addTab(new QWidget, title);
        new HoverCloseButtons(tabs.tabBar());
        tabs.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tabs));
        QTabBar *bar = tabs.tabBar();
        const auto side = static_cast<QTabBar::ButtonPosition>(
            bar->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, bar));
        const auto isShown = [bar, side](int index) {
            const auto *effect = qobject_cast<QGraphicsOpacityEffect *>(bar->tabButton(index, side)->graphicsEffect());
            return !effect || effect->opacity() > 0;
        };
        QTRY_VERIFY(isShown(0) && !isShown(1) && !isShown(2));

        const QPointF overThree = bar->tabRect(2).center();
        QHoverEvent move(QEvent::HoverMove, overThree, bar->mapToGlobal(overThree), overThree);
        QApplication::sendEvent(bar, &move);
        QTRY_VERIFY(isShown(2));
        QHoverEvent leave(QEvent::HoverLeave, QPointF(-1, -1), QPointF(-1, -1), overThree);
        QApplication::sendEvent(bar, &leave);
        QTRY_VERIFY(!isShown(2));

        tabs.setCurrentIndex(1);
        QTRY_VERIFY(isShown(1) && !isShown(0));
        tabs.addTab(new QWidget, "four"); // a new tab's button too
        QTRY_VERIFY(!isShown(3));
    }

    void addButtonFollowsTheLastTab()
    {
        QTabWidget tabs;
        tabs.setDocumentMode(true);
        tabs.resize(800, 300);
        auto *button = new QToolButton;
        button->setText("+");
        new TrailingTabButton(&tabs, button);
        tabs.addTab(new QWidget, "first");
        tabs.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tabs));

        const auto afterLastTab = [&] {
            const QTabBar *bar = tabs.tabBar();
            return bar->geometry().left() + bar->tabRect(bar->count() - 1).right() + 1;
        };
        QTRY_VERIFY(button->x() >= afterLastTab() && button->x() <= afterLastTab() + 8);
        const int first = button->x();
        tabs.addTab(new QWidget, "second tab");
        QTRY_VERIFY(button->x() > first);
        QVERIFY(button->x() >= afterLastTab() && button->x() <= afterLastTab() + 8);
        tabs.removeTab(1);
        QTRY_COMPARE(button->x(), first);

        // Tabs filling the bar: the button stays inside the widget.
        for (int i = 0; i < 30; ++i)
            tabs.addTab(new QWidget, QString("terminal %1").arg(i));
        QTRY_VERIFY(button->geometry().right() < tabs.width());
        QVERIFY(button->isVisible());
    }

    void terminalTabsAreAsTallAsEditorTabs()
    {
        ThemeStyle::apply(Theme::kodamiko());
        EditorTabs editors(Theme::kodamiko());
        editors.addTab(new QLabel("code"), "main.cpp");
        QTabWidget terminals;
        terminals.setObjectName("terminalTabs");
        terminals.setTabsClosable(true);
        terminals.setDocumentMode(true);
        terminals.addTab(new QLabel("shell"), "Terminal");

        editors.show();
        terminals.show();

        QTRY_VERIFY(editors.tabBar()->height() > 0);
        QCOMPARE(terminals.tabBar()->height(), editors.tabBar()->height());
    }

    void baseLineRunsUnderTheTabsButTheCurrentOne()
    {
        QTabWidget tabs;
        tabs.setDocumentMode(true);
        tabs.addTab(new QWidget, "first");
        tabs.addTab(new QWidget, "second");
        auto *line = new TabBarBaseLine(tabs.tabBar());
        line->setColor(Qt::red);
        tabs.resize(600, 200);
        tabs.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tabs));

        QTabBar *bar = tabs.tabBar();
        QVERIFY(!bar->drawBase()); // the style's own, at the top in some styles
        const auto colorUnder = [&](int tab) {
            const QImage image = bar->grab().toImage();
            return image.pixelColor(bar->tabRect(tab).center().x(), bar->height() - 1);
        };
        QCOMPARE(colorUnder(1), QColor(Qt::red));
        QVERIFY(colorUnder(0) != QColor(Qt::red));
        tabs.setCurrentIndex(1);
        QTRY_VERIFY(colorUnder(1) != QColor(Qt::red));
        QCOMPARE(colorUnder(0), QColor(Qt::red));
    }

    void problemsHeaderCountsAndFoldsTheProblems()
    {
        LanguageServers servers;
        ProblemStore store(&servers);
        Problem warning;
        warning.path = "/project/main.cpp";
        warning.severity = Diagnostic::Severity::Warning;
        warning.message = "unused variable";
        Problem error = warning;
        error.severity = Diagnostic::Severity::Error;
        error.message = "no member named foo";
        Problem other = error;
        other.path = "/project/other.cpp";
        store.setBuildProblems("build", {warning, error, other});
        ProblemsView view(&store);
        view.setProjectFolder("/project");

        const auto *header = view.findChild<ViewHeader *>();
        QVERIFY(header);
        const auto texts = [header] {
            QStringList result;
            for (const QLabel *label : header->findChildren<QLabel *>()) {
                if (label->isVisibleTo(header))
                    result.append(label->text());
            }
            return result;
        };
        QTRY_COMPARE(view.tree()->topLevelItemCount(), 2);
        QVERIFY(texts().contains("Problems"));
        QVERIFY(texts().join(' ').contains(QString::number(2))); // two errors

        const QList<QToolButton *> buttons = header->findChildren<QToolButton *>();
        QCOMPARE(buttons.size(), 2); // Expand All, Collapse All
        buttons.at(1)->click();
        QVERIFY(!view.tree()->topLevelItem(0)->isExpanded());
        QVERIFY(!view.tree()->topLevelItem(1)->isExpanded());
        buttons.at(0)->click();
        QVERIFY(view.tree()->topLevelItem(0)->isExpanded());
        QVERIFY(view.tree()->topLevelItem(1)->isExpanded());
    }

    void endButtonKeepsTheEndOfTheTabBar()
    {
        QTabWidget tabs;
        tabs.setDocumentMode(true);
        tabs.resize(800, 300);
        auto *add = new QToolButton;
        add->setText("+");
        auto *end = new QToolButton;
        end->setText("E");
        auto *trailing = new TrailingTabButton(&tabs, add);
        trailing->setEndButton(end);
        tabs.addTab(new QWidget, "first");
        tabs.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tabs));

        QTRY_VERIFY(end->geometry().right() < tabs.width());
        QVERIFY(end->x() > tabs.width() - end->width() - 8);
        // Square buttons, centered rather than as tall as the bar.
        const QRect bar = tabs.tabBar()->geometry();
        for (const QToolButton *button : {add, end}) {
            QCOMPARE(button->width(), button->height());
            QVERIFY(button->height() <= bar.height());
            QCOMPARE(button->y() - bar.top(), bar.bottom() - button->geometry().bottom());
        }

        // Tabs filling the bar: "+" stays before the end button.
        for (int i = 0; i < 30; ++i)
            tabs.addTab(new QWidget, QString("terminal %1").arg(i));
        QTRY_VERIFY(add->geometry().right() < end->x());
        QVERIFY(end->isVisible());
    }

    void detachedPageMovesToAWindowAndComesBackWhenItCloses()
    {
        QSettings().remove("detachTest");
        QStackedWidget stack;
        auto *first = new QLabel("first", &stack);
        auto *page = new QLabel("page", &stack);
        stack.addWidget(first);
        stack.addWidget(page);
        stack.setCurrentWidget(page);
        stack.show();
        auto *detachable = new DetachablePage(&stack, page, "detachTest", {"Window", "Away", "Back"});
        const QSignalSpy changed(detachable, &DetachablePage::detachedChanged);

        detachable->detach();

        QVERIFY(detachable->isDetached());
        QCOMPARE(stack.indexOf(page), -1);
        QCOMPARE(stack.currentIndex(), 1); // the placeholder, in the page's place
        QVERIFY(page->window() != stack.window());
        QVERIFY(page->window()->isVisible());
        QCOMPARE(page->window()->windowTitle(), QString("Window"));

        page->window()->close();

        QVERIFY(!detachable->isDetached());
        QCOMPARE(stack.indexOf(page), 1);
        QCOMPARE(stack.currentWidget(), page);
        QCOMPARE(changed.size(), 2);
        QSettings().remove("detachTest");
    }

    void raisingTheDetachedWindowKeepsItMaximized()
    {
        QSettings().remove("detachTest");
        QStackedWidget stack;
        auto *page = new QLabel("page", &stack);
        stack.addWidget(page);
        auto *detachable = new DetachablePage(&stack, page, "detachTest", {"Window", "Away", "Back"});
        detachable->detach();
        page->window()->showMaximized();
        QTRY_VERIFY(page->window()->isMaximized());

        detachable->raiseWindow(); // as a new terminal does

        QVERIFY(page->window()->isMaximized());
        page->window()->showMinimized();
        detachable->raiseWindow();
        QVERIFY(!page->window()->isMinimized());
        QVERIFY(page->window()->isMaximized());
        detachable->attach();
        QSettings().remove("detachTest");
    }

    void detachedPageStartsBackInTheStackAndDetachesWhereItWas()
    {
        QSettings().remove("detachTest");
        const QRect placed(120, 90, 500, 300);
        {
            QStackedWidget stack;
            auto *page = new QLabel("page", &stack);
            stack.addWidget(page);
            auto *detachable = new DetachablePage(&stack, page, "detachTest", {"Window", "Away", "Back"});
            detachable->detach();
            page->window()->setGeometry(placed);
        } // closing the application with the page away

        QStackedWidget stack;
        auto *page = new QLabel("page", &stack);
        stack.addWidget(page);
        auto *detachable = new DetachablePage(&stack, page, "detachTest", {"Window", "Away", "Back"});

        QVERIFY(!detachable->isDetached());
        QCOMPARE(stack.indexOf(page), 0);

        detachable->detach();

        QVERIFY(detachable->isDetached());
        QCOMPARE(page->window()->geometry().size(), placed.size());
        QSettings().remove("detachTest");
    }
};

QTEST_MAIN(TestWorkspace)
#include "tst_workspace.moc"
