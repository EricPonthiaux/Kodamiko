// SPDX-License-Identifier: GPL-3.0-only

#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "ui/FileIcons.h"
#include "workspace/EntryPopups.h"
#include "workspace/FileTreeView.h"
#include "workspace/FileUndoStack.h"
#include "workspace/TreeDecorations.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QPointer>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QtTest>

namespace {

void write(const QString &path, const QByteArray &content = {})
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

// Names of the rows the tree shows, top to bottom.
QStringList shownNames(const FileTreeView &tree)
{
    QStringList names;
    for (QModelIndex index = tree.model()->index(0, 0, tree.rootIndex()); index.isValid();
         index = tree.indexBelow(index))
        names.append(QFileInfo(index.data(TreeRoles::path).toString()).fileName());
    return names;
}

// Selects `paths` in the tree, as Ctrl+clicks would.
void selectAll(FileTreeView &tree, const QStringList &paths)
{
    // select() makes each row the only selected one: the rows first.
    QList<QPersistentModelIndex> rows;
    for (const QString &path : paths) {
        QVERIFY(tree.select(path));
        QTRY_COMPARE(tree.currentPath(), path);
        rows.append(tree.currentIndex());
    }
    for (const QPersistentModelIndex &row : rows)
        tree.selectionModel()->select(row, QItemSelectionModel::Select | QItemSelectionModel::Rows);
}

} // namespace

class TestFileTree : public QObject {
    Q_OBJECT

private slots:

    void folderIconIsDrawnAtTheSizeAsked()
    {
        const QImage icon = FileIcons::folder(true).pixmap(QSize(32, 32), 1.0).toImage();

        QCOMPARE(icon.size(), QSize(32, 32));
        // Drawn across the whole square, not a 16-pixel picture in its corner.
        int right = 0;
        for (int y = 0; y < icon.height(); ++y) {
            for (int x = 0; x < icon.width(); ++x) {
                if (qAlpha(icon.pixel(x, y)) > 0)
                    right = std::max(right, x);
            }
        }
        QVERIFY(right >= 26);
    }

    void folderIconIsANeutralGray()
    {
        for (const bool dark : {true, false}) {
            const QImage icon = FileIcons::folder(dark).pixmap(QSize(32, 32), 1.0).toImage();
            QColor outline;
            for (int x = 0; x < icon.width() && !outline.isValid(); ++x) {
                if (qAlpha(icon.pixel(x, 16)) == 255)
                    outline = QColor(icon.pixel(x, 16));
            }
            QVERIFY(outline.isValid());
            QVERIFY2(outline.hsvSaturation() < 40, qPrintable(outline.name())); // no violet
        }
    }

    void problemsColorFilesAndTheirFoldersBeforeGitStates()
    {
        const QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.path());
        write(root + "/src/broken.cpp");
        write(root + "/src/fine.cpp");
        FileTreeView tree;
        tree.setRootPath(root);
        tree.setStatuses(
            {{root + "/src/broken.cpp", FileStatus::Modified}, {root + "/src/fine.cpp", FileStatus::Modified}}, true);
        tree.setProblems({{root + "/src/broken.cpp", {.errors = 2, .warnings = 1}}});
        tree.show();

        QVERIFY(tree.select(root + "/src/broken.cpp"));
        QTRY_COMPARE(tree.currentPath(), root + "/src/broken.cpp");
        const QModelIndex broken = tree.currentIndex();
        QCOMPARE(broken.data(TreeRoles::problemCount).toInt(), 3);
        QCOMPARE(broken.data(TreeRoles::statusLetter).toString(), QString("M"));
        const auto errorColor = broken.data(Qt::ForegroundRole).value<QColor>();
        QCOMPARE(errorColor, broken.data(TreeRoles::problemColor).value<QColor>());
        QVERIFY(errorColor != broken.data(TreeRoles::statusColor).value<QColor>());
        QVERIFY(broken.data(Qt::ToolTipRole).toString().contains("2 error(s)"));
        QCOMPARE(broken.parent().data(Qt::ForegroundRole).value<QColor>(), errorColor);
        QVERIFY(broken.parent().data(Qt::ToolTipRole).toString().contains("error(s) in its files"));

        QVERIFY(tree.select(root + "/src/fine.cpp"));
        QTRY_COMPARE(tree.currentPath(), root + "/src/fine.cpp");
        QCOMPARE(tree.currentIndex().data(TreeRoles::problemCount).toInt(), 0);
        QCOMPARE(tree.currentIndex().data(Qt::ForegroundRole).value<QColor>(),
                 tree.currentIndex().data(TreeRoles::statusColor).value<QColor>());
    }

    void openFilesAreMarked()
    {
        const QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.path());
        write(root + "/open.txt");
        write(root + "/closed.txt");
        FileTreeView tree;
        tree.setRootPath(root);
        tree.setOpenFiles({root + "/open.txt"});
        tree.show();
        QVERIFY(tree.select(root + "/open.txt"));
        QTRY_COMPARE(tree.currentPath(), root + "/open.txt");
        QVERIFY(tree.currentIndex().data(TreeRoles::isOpen).toBool());
        QVERIFY(tree.select(root + "/closed.txt"));
        QTRY_COMPARE(tree.currentPath(), root + "/closed.txt");
        QVERIFY(!tree.currentIndex().data(TreeRoles::isOpen).toBool());
    }

    void tooltipGivesSizeAndDate()
    {
        const QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.path());
        write(root + "/notes.txt", QByteArray(2048, 'x'));
        FileTreeView tree;
        tree.setRootPath(root);
        tree.show();
        QVERIFY(tree.select(root + "/notes.txt"));
        QTRY_COMPARE(tree.currentPath(), root + "/notes.txt");
        const QString toolTip = tree.currentIndex().data(Qt::ToolTipRole).toString();
        QVERIFY(toolTip.contains(QLocale().formattedDataSize(2048)));
        QVERIFY(toolTip.contains("modified"));
    }

    void previewTabGivesWayToTheNextPreview()
    {
        const QTemporaryDir dir;
        for (const char *name : {"a.txt", "b.txt", "c.txt"})
            write(dir.filePath(name), "text\n");
        EditorTabs tabs(Theme::kodamiko());

        QVERIFY(tabs.openPreview(dir.filePath("a.txt")));
        QVERIFY(tabs.tabBar()->property("preview").toBool());
        QVERIFY(tabs.openPreview(dir.filePath("b.txt")));
        QCOMPARE(tabs.openFilePaths(), QStringList({dir.filePath("b.txt")}));

        // A file opened for good goes before the preview, which stays last.
        QVERIFY(tabs.openFile(dir.filePath("c.txt")));
        QCOMPARE(tabs.openFilePaths(), QStringList({dir.filePath("c.txt"), dir.filePath("b.txt")}));

        // Opened for good: no longer a preview.
        QVERIFY(tabs.openFile(dir.filePath("b.txt")));
        QVERIFY(!tabs.tabBar()->property("preview").toBool());
        QVERIFY(tabs.openPreview(dir.filePath("a.txt")));
        QCOMPARE(tabs.openFilePaths().size(), 3);
    }

    void editingThePreviewKeepsItOpen()
    {
        const QTemporaryDir dir;
        for (const char *name : {"a.txt", "b.txt"})
            write(dir.filePath(name), "text\n");
        EditorTabs tabs(Theme::kodamiko());
        QVERIFY(tabs.openPreview(dir.filePath("a.txt")));
        tabs.editorFor(dir.filePath("a.txt"))->insert("x");
        QVERIFY(!tabs.tabBar()->property("preview").toBool());
        QVERIFY(tabs.openPreview(dir.filePath("b.txt")));
        QCOMPARE(tabs.openFilePaths(), QStringList({dir.filePath("a.txt"), dir.filePath("b.txt")}));
    }

    void replacingThePreviewShowsNoOtherTabMeanwhile()
    {
        const QTemporaryDir dir;
        for (const char *name : {"kept.txt", "a.txt", "b.txt"})
            write(dir.filePath(name), "text\n");
        EditorTabs tabs(Theme::kodamiko());
        QVERIFY(tabs.openFile(dir.filePath("kept.txt")));
        QVERIFY(tabs.openPreview(dir.filePath("a.txt")));
        QStringList shown;
        connect(&tabs, &QTabWidget::currentChanged, &tabs, [&shown, &tabs] { shown.append(tabs.currentFilePath()); });

        QVERIFY(tabs.openPreview(dir.filePath("b.txt")));

        // The file tree follows the current tab: kept.txt in between made it jump there.
        QCOMPARE(shown, QStringList({dir.filePath("b.txt")}));
        QCOMPARE(tabs.openFilePaths(), QStringList({dir.filePath("kept.txt"), dir.filePath("b.txt")}));
    }

    void aPreviewThatCannotOpenLeavesThePreviousOne()
    {
        const QTemporaryDir dir;
        write(dir.filePath("a.txt"), "text\n");
        EditorTabs tabs(Theme::kodamiko());
        QVERIFY(tabs.openPreview(dir.filePath("a.txt")));

        QVERIFY(!tabs.openPreview(dir.filePath("missing.txt")));

        QCOMPARE(tabs.openFilePaths(), QStringList({dir.filePath("a.txt")}));
        QVERIFY(tabs.tabBar()->property("preview").toBool());
    }

    void closingAllTabsShowsNoOtherTabMeanwhile()
    {
        const QTemporaryDir dir;
        EditorTabs tabs(Theme::kodamiko());
        for (const char *name : {"a.txt", "b.txt", "c.txt"}) {
            write(dir.filePath(name), "text\n");
            QVERIFY(tabs.openFile(dir.filePath(name)));
        }
        QStringList shown;
        connect(&tabs, &QTabWidget::currentChanged, &tabs, [&shown, &tabs] { shown.append(tabs.currentFilePath()); });

        QVERIFY(tabs.closeAll());

        // The file tree follows the current tab: b.txt and a.txt in between made it run through them.
        shown.removeDuplicates(); // c.txt again as each tab before it closes
        QCOMPARE(shown, QStringList({dir.filePath("c.txt"), QString()}));
    }

    void deletingAFolderShowsNoneOfItsFilesMeanwhile()
    {
        const QTemporaryDir dir;
        EditorTabs tabs(Theme::kodamiko());
        for (const char *name : {"folder/a.txt", "folder/b.txt", "kept.txt"}) {
            write(dir.filePath(name), "text\n");
            QVERIFY(tabs.openFile(dir.filePath(name)));
        }
        tabs.setCurrentIndex(0); // folder/a.txt
        QStringList shown;
        connect(&tabs, &QTabWidget::currentChanged, &tabs, [&shown, &tabs] { shown.append(tabs.currentFilePath()); });

        tabs.closeFilesUnder(dir.filePath("folder"));

        // folder/b.txt in between made the file tree, which follows the current tab, jump there.
        QCOMPARE(shown, QStringList({dir.filePath("kept.txt")}));
    }

    void severalSelectedEntriesAreCutAndPastedTogether()
    {
        const QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.filePath("project"));
        write(root + "/src/a.cpp");
        write(root + "/src/b.cpp");
        write(root + "/lib/keep.txt");
        FileTreeView tree;
        tree.setRootPath(root);
        tree.show();
        selectAll(tree, {root + "/src/a.cpp", root + "/src/b.cpp"});
        QCOMPARE(tree.selectedPaths().size(), 2);

        const QSignalSpy send(&tree, &FileTreeView::sendToClaudeRequested);
        QTest::keyClick(&tree, Qt::Key_K, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(send.size(), 2);

        QTest::keyClick(&tree, Qt::Key_X, Qt::ControlModifier);
        QVERIFY(tree.select(root + "/lib"));
        QTRY_COMPARE(tree.currentPath(), root + "/lib");
        QTest::keyClick(&tree, Qt::Key_V, Qt::ControlModifier);
        QVERIFY(QFileInfo::exists(root + "/lib/a.cpp"));
        QVERIFY(QFileInfo::exists(root + "/lib/b.cpp"));
        QVERIFY(!QFileInfo::exists(root + "/src/a.cpp"));
    }

    void entriesInsideASelectedFolderGoWithIt()
    {
        const QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.path());
        write(root + "/src/a.cpp");
        FileTreeView tree;
        tree.setRootPath(root);
        tree.show();
        selectAll(tree, {root + "/src", root + "/src/a.cpp"});
        QCOMPARE(tree.selectedPaths(), QStringList({root + "/src"}));
    }

    void filterWhileTypingShowsOnlyTheMatches()
    {
        const QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.filePath("project"));
        write(root + "/src/parser.cpp");
        write(root + "/src/lexer.cpp");
        write(root + "/docs/guide.md");
        FileTreeView tree;
        tree.setRootPath(root);
        tree.setFilterWhileTyping(true);
        tree.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tree));
        tree.restoreExpandedPaths({root + "/src", root + "/docs"});
        QTRY_VERIFY(shownNames(tree).contains("guide.md") && shownNames(tree).contains("lexer.cpp"));
        QVERIFY(tree.select(root + "/docs"));
        QTRY_COMPARE(tree.currentPath(), root + "/docs");

        QTest::keyClicks(&tree, "pars");
        QTRY_COMPARE(shownNames(tree), QStringList({"project", "src", "parser.cpp"}));
        QCOMPARE(tree.currentPath(), root + "/src/parser.cpp");

        // Back as it was, on the match chosen.
        QTest::keyClick(&tree, Qt::Key_Escape);
        QTRY_VERIFY(shownNames(tree).contains("guide.md") && shownNames(tree).contains("lexer.cpp"));
        QTRY_COMPARE(tree.currentPath(), root + "/src/parser.cpp");
    }

    void selectingAFileKeepsTheHorizontalScroll()
    {
        const QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.path());
        const QString deepFolder = root + "/a/b/c/d/e/f";
        write(deepFolder + "/aFileWhoseNameIsLongerThanTheTreeIsWide.txt");
        write(deepFolder + "/short.txt");
        FileTreeView tree;
        tree.setRootPath(root);
        tree.resize(150, 300);
        tree.show();
        QVERIFY(tree.select(deepFolder + "/aFileWhoseNameIsLongerThanTheTreeIsWide.txt"));
        QTRY_VERIFY(tree.horizontalScrollBar()->maximum() > 0);
        // Its folders scrolled out of sight, the names still in view.
        tree.horizontalScrollBar()->setValue(tree.indentation());
        const int scrolled = tree.horizontalScrollBar()->value();
        QVERIFY(scrolled > 0);

        QVERIFY(tree.select(deepFolder + "/short.txt"));
        QTRY_COMPARE(tree.currentPath(), deepFolder + "/short.txt");

        QCOMPARE(tree.horizontalScrollBar()->value(), scrolled);
    }

    void selectingAFileOutOfSightBringsItsNameIntoView()
    {
        const QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.path());
        write(root + "/a/b/c/d/e/f/aFileWhoseNameIsLongerThanTheTreeIsWide.txt");
        write(root + "/top.txt");
        FileTreeView tree;
        tree.setRootPath(root);
        tree.resize(150, 300);
        tree.show();
        QVERIFY(tree.select(root + "/a/b/c/d/e/f/aFileWhoseNameIsLongerThanTheTreeIsWide.txt"));
        QTRY_VERIFY(tree.horizontalScrollBar()->maximum() > 0);
        tree.horizontalScrollBar()->setValue(tree.horizontalScrollBar()->maximum());

        QVERIFY(tree.select(root + "/top.txt"));
        QTRY_COMPARE(tree.currentPath(), root + "/top.txt");

        const QRect name = tree.visualRect(tree.currentIndex());
        QVERIFY(name.left() >= 0);
        QVERIFY(name.left() < tree.viewport()->width());
    }

    void undoBringsBackARenamedFile()
    {
        const QTemporaryDir dir;
        const QString root = QDir::cleanPath(dir.path());
        write(root + "/old.txt");
        FileTreeView tree;
        tree.setRootPath(root);
        tree.show();
        QSignalSpy renamed(&tree, &FileTreeView::entryRenamed);
        QVERIFY(tree.select(root + "/old.txt"));
        QTRY_COMPARE(tree.currentPath(), root + "/old.txt");
        QTest::keyClick(&tree, Qt::Key_F2);
        const QPointer<NamePopup> popup = tree.findChild<NamePopup *>();
        QVERIFY(popup);
        popup->field()->setText("new.txt");
        QTest::keyClick(popup->field(), Qt::Key_Return);
        QVERIFY(QFileInfo::exists(root + "/new.txt"));

        QTest::keyClick(&tree, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(QFileInfo::exists(root + "/old.txt"));
        QVERIFY(!QFileInfo::exists(root + "/new.txt"));
        QCOMPARE(renamed.size(), 2);
        QCOMPARE(renamed.last().at(1).toString(), root + "/old.txt");
    }

    void undoTakesBackFromTheTrash()
    {
        // A freedesktop.org trash of our own: the system's stays untouched.
        const QTemporaryDir dir;
        write(dir.filePath("Trash/files/a.txt"));
        write(dir.filePath("Trash/info/a.txt.trashinfo"));
        FileUndoStack stack;
        stack.record({.kind = FileUndoStack::Step::Kind::Trashed,
                      .path = dir.filePath("project/a.txt"),
                      .source = dir.filePath("Trash/files/a.txt")});
        QVERIFY(QDir().mkpath(dir.filePath("project")));
        QCOMPARE(stack.lastDescription(), QString("Undo Deletion of “a.txt”"));
        QCOMPARE(stack.undo(), QString());
        QVERIFY(QFileInfo::exists(dir.filePath("project/a.txt")));
        QVERIFY(!QFileInfo::exists(dir.filePath("Trash/info/a.txt.trashinfo")));
        QVERIFY(!stack.last());
    }

    void undoNeverOverwrites()
    {
        const QTemporaryDir dir;
        write(dir.filePath("a.txt"));
        write(dir.filePath("b.txt"));
        FileUndoStack stack;
        stack.record(
            {.kind = FileUndoStack::Step::Kind::Moved, .path = dir.filePath("b.txt"), .source = dir.filePath("a.txt")});
        QVERIFY(!stack.undo().isEmpty()); // a.txt exists again
        QVERIFY(QFileInfo::exists(dir.filePath("b.txt")));
        QVERIFY(!stack.last()); // forgotten all the same
    }
};

QTEST_MAIN(TestFileTree)
#include "tst_filetree.moc"
