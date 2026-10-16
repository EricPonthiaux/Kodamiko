#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "git/GitProcess.h"
#include "ui/OverlayPopup.h"
#include "ui/ThemeStyle.h"
#include "ui/TitleBar.h"
#include "ui/TrailingTabButton.h"
#include "ui/TreeMetrics.h"
#include "ui/WindowFrame.h"
#include "workspace/CommandPalette.h"
#include "workspace/EntryPopups.h"
#include "workspace/FileIcons.h"
#include "workspace/FileOperations.h"
#include "workspace/FilesPanel.h"
#include "workspace/FileTreeView.h"
#include "workspace/ProjectTreeModel.h"
#include "workspace/SidePanel.h"

#include <QClipboard>
#include <QSignalSpy>
#include <QComboBox>
#include <QFile>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QTabBar>
#include <QSettings>
#include <QSizeGrip>
#include <QMenu>
#include <QMenuBar>
#include <QToolButton>
#include <QMimeData>
#include <QStandardPaths>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QPointer>
#include <QScrollBar>
#include <QTemporaryDir>
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
    void paletteMatchesLettersInOrder()
    {
        QVERIFY(CommandPalette::matchScore("Git: Stage All", "stall") > 0);
        QCOMPARE(CommandPalette::matchScore("Git: Stage All", "llats"), -1);
        QCOMPARE(CommandPalette::matchScore("View: Diff Mode", ""), 0);
        // Word starts and consecutive letters rank first.
        QVERIFY(CommandPalette::matchScore("File: Save All", "save")
                > CommandPalette::matchScore("Navigate: Go to Definition", "save"));
    }

    void longNamesScrollHorizontally()
    {
        QTemporaryDir dir;
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
        QTemporaryDir dir;
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
        QAbstractItemDelegate *custom = tree.itemDelegate();
        applyTreeMetrics(&tree, QApplication::font());
        QVERIFY(tree.itemDelegate() == custom);
        QTreeView plain;
        QAbstractItemDelegate *standard = plain.itemDelegate();
        applyTreeMetrics(&plain, QApplication::font());
        QVERIFY(plain.itemDelegate() != standard);
    }

    void overlayPopupsAreCenteredAndResizable()
    {
        QSettings().remove("overlays/test");
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
        QMouseEvent move(QEvent::MouseMove, QPointF(edge + QPoint(100, 0)), popup.mapToGlobal(QPointF(edge + QPoint(100, 0))),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&popup, &move);
        QTest::mouseRelease(&popup, Qt::LeftButton, {}, edge + QPoint(100, 0));
        QCOMPARE(popup.width(), (1000 - 48) / 2 + 2 * shadow + 100);
        const QSize chosen = QSettings().value("overlays/test").toSize();
        QCOMPARE(chosen, popup.size() - QSize(2 * shadow, 2 * shadow));

        QTest::mouseClick(button, Qt::LeftButton); // a click elsewhere closes it
        QVERIFY(!popup.isVisible());
        popup.showCentered();
        QCOMPARE(popup.size(), chosen + QSize(2 * shadow, 2 * shadow));
        window.resize(500, 400); // too small: the panel shrinks to fit
        QVERIFY(popup.width() <= 500);
        popup.dismiss();
        QVERIFY(!popup.isVisible());
        popup.resetSize();
        QVERIFY(!QSettings().contains("overlays/test"));
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
        ThemeStyle::apply(Theme::purple());
        ThemeStyle::apply(Theme::light());
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
        QSignalSpy visibility(panel, &SidePanel::panelVisibilityChanged);
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
        titleBar->setProject("duet", "/p/duet", project);
        titleBar->setClaudeState({}, "Claude is working", "details");
        QStringList texts;
        for (QToolButton *button : titleBar->findChildren<QToolButton *>())
            if (button->isVisibleTo(titleBar))
                texts.append(button->text());
        QVERIFY(texts.contains("duet  ▾"));
        QVERIFY(texts.contains("Claude is working"));
        titleBar->setClaudeState({}, {}, {});

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
        QTemporaryDir dir;
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
        QTemporaryDir dir;
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
        QTemporaryDir dir;
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
        QTemporaryDir dir;
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
        QTemporaryDir dir;
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
        QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.filePath("project"));
        write(root + "/src/a.cpp");
        write(root + "/lib/keep.txt");
        FileTreeView tree;
        tree.setRootPath(root);
        tree.show();
        QSignalSpy renamed(&tree, &FileTreeView::entryRenamed);
        QSignalSpy created(&tree, &FileTreeView::entryCreated);

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
        QVERIFY(tree.select(root + "/lib/keep.txt")); // a file: its folder
        QTest::keyClick(&tree, Qt::Key_V, Qt::ControlModifier); // name taken: a popup asks
        QPointer<NamePopup> popup = tree.findChild<NamePopup *>();
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
        QVERIFY(!clipboard || !clipboard->hasFormat("application/x-duet-cut"));
    }

    void treeDuplicatesUnderANewName()
    {
        QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.filePath("project"));
        write(root + "/Widget.cpp");
        FileTreeView tree;
        tree.setRootPath(root);
        tree.show();
        QVERIFY(tree.select(root + "/Widget.cpp"));
        QTest::keyClick(&tree, Qt::Key_F5);
        QPointer<NamePopup> popup = tree.findChild<NamePopup *>();
        QVERIFY(popup);
        QCOMPARE(popup->field()->text(), QString("Widget_copy.cpp"));
        QCOMPARE(popup->field()->selectedText(), QString("Widget_copy"));
        QTest::keyClick(popup->field(), Qt::Key_Return);
        QVERIFY(QFileInfo::exists(root + "/Widget_copy.cpp"));
    }

    void treeColorsFilesByGitState()
    {
        QTemporaryDir dir;
        write(dir.filePath("src/a.cpp"));
        write(dir.filePath("b.txt"));
        const QString root = QDir::cleanPath(dir.path());
        FileTreeView tree;
        tree.setRootPath(root);
        tree.setStatuses({{root + "/src/a.cpp", FileStatus::Modified}, {root + "/b.txt", FileStatus::Untracked}}, true);
        tree.setClaudeTouched({root + "/b.txt"});
        tree.show();
        QVERIFY(tree.select(root + "/src/a.cpp"));
        QTRY_COMPARE(tree.currentPath(), root + "/src/a.cpp");

        const QModelIndex file = tree.currentIndex();
        const QModelIndex src = file.parent();
        const QColor modified = file.data(Qt::ForegroundRole).value<QColor>();
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
        QTemporaryDir dir;
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
        QTemporaryDir dir;
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

        QSignalSpy activated(&tree, &FileTreeView::fileActivated);
        QTest::keyClicks(&tree, "alp");
        QCOMPARE(tree.currentPath(), root + "/alpha.cpp");
        QTest::keyClick(&tree, Qt::Key_Return);
        QCOMPARE(activated.size(), 1);
        QVERIFY(tree.speedSearch().isEmpty());
    }

    void treeCopiesPathsAndSendsToClaude()
    {
        QTemporaryDir dir;
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
        QTemporaryDir dir;
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
        QPointer<NamePopup> popup = new NamePopup(
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
        QPointer<NamePopup> popup = new NamePopup(
            "New Folder", "project/", [&applied](const QString &) { applied = true; return QString(); }, &window);
        popup->popup();
        QVERIFY(QTest::qWaitForWindowExposed(popup));
        QTest::keyClick(popup->field(), Qt::Key_Escape);
        QTRY_VERIFY(!popup);
        QVERIFY(!applied);
    }

    void renameNamesAreChecked()
    {
        QTemporaryDir dir;
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
            "Delete File", "project/", "Delete “a.txt”?", "Delete",
            [&calls](ConfirmPopup *popup) {
                // First call: asks something else, as when the trash fails.
                if (++calls == 1) {
                    popup->setMessage("Delete it permanently?", "Delete Permanently");
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
        popup = new ConfirmPopup("Delete File", "project/", "Delete?", "Delete",
                                 [&applied](ConfirmPopup *) { return applied = true; }, &window);
        popup->popup();
        QVERIFY(QTest::qWaitForWindowExposed(popup));
        popup->cancelButton()->setFocus();
        QTest::keyClick(popup->cancelButton(), Qt::Key_Return);
        QTRY_VERIFY(!popup);
        QVERIFY(!applied);
    }

    void tabsFollowARenamedFolder()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("src"));
        for (const char *name : {"src/a.txt", "b.txt"}) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("one\ntwo\nthree\n");
        }
        EditorTabs tabs(Theme::dark());
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
        int line = 0, column = 0;
        tabs.editorFor(dir.filePath("lib/a.txt"))->getCursorPosition(&line, &column);
        QCOMPARE(line, 2);
        QCOMPARE(column, 1);
        QCOMPARE(tabs.currentFilePath(), dir.filePath("b.txt"));

        tabs.closeFilesUnder(dir.filePath("b.txt")); // unsaved changes discarded, no question
        QCOMPARE(tabs.openFilePaths(), QStringList({dir.filePath("lib/a.txt")}));
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
};

QTEST_MAIN(TestWorkspace)
#include "tst_workspace.moc"
