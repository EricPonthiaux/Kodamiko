#pragma once

#include "ProjectTreeModel.h"
#include "git/FileStatuses.h"

#include <QSet>
#include <QTreeView>

#include <functional>

class QLabel;
class TreeDecorations;

// Tree of the working folder; a double-click opens the file.
// The context menu creates files and folders in the clicked folder, renames
// (F2, Shift+F6) and deletes (Delete) the clicked entry, copies its path,
// sends it to Claude, opens a terminal there, and runs git commands on it.
// The folder itself is the first row, expanded on opening.
// Names are never truncated: when too wide, a horizontal scroll bar
// appears at the bottom.
//
// As in IntelliJ's Project view: names are colored by their git state
// (folders too, from the files they contain), typing a few letters starts a
// speed search, and a scope restricts the tree to the changed files or to
// those edited by Claude. A dot on the icon marks the files Claude has
// just written.
class FileTreeView : public QTreeView {
    Q_OBJECT

public:
    enum class Scope { Project, ChangedFiles, EditedByClaude };

    explicit FileTreeView(QWidget *parent = nullptr);
    ~FileTreeView() override;

    void setRootPath(const QString &path);
    QString rootPath() const { return rootPath_; }

    // Compact folders, file nesting, ignored files (see ProjectTreeModel).
    ProjectTreeModel::Options treeOptions() const;
    void setTreeOptions(const ProjectTreeModel::Options &options);
    // File icons for a dark or light background.
    void setDarkIcons(bool dark);
    // Rereads the folders shown now, without waiting for the disk watcher.
    void refreshFromDisk();

    // Expanded (visible) folders, to restore the tree's layout. In a scope
    // other than Project, those of the Project scope.
    QStringList expandedPaths() const;
    // Folders are read lazily: each path is expanded as soon as
    // its entry appears in the tree.
    void restoreExpandedPaths(const QStringList &paths);

    // Ask for a name in a popup and create it in the folder of the current
    // item (the working folder when nothing is selected). A new file is
    // then opened through fileActivated.
    void createFile();
    void createFolder();
    // Ask for a name and create a blank diagram of the kind `kindId` (see
    // DiagramKinds), opened through fileActivated.
    void createDiagram(const QString &kindId);
    // Emits generateDiagramRequested() for the folder of the current item.
    void generateDiagram();
    // Ask for a new name, or for confirmation, for the current item (never
    // the working folder itself). Deleting moves to the trash when possible.
    void renameCurrent();
    void deleteCurrent();

    // Clipboard, as in a file manager: Ctrl+X, Ctrl+C, Ctrl+V. Pasting a cut
    // item moves it, otherwise copies (files copied in a file manager too).
    // A name already taken is asked for again in a popup.
    void cutCurrent();
    void copyCurrent();
    void paste();
    // F5: copy of the current item in the same folder, under a new name.
    void duplicateCurrent();

    // Whether files open under a path have unsaved changes: renaming is
    // then refused, and deleting warns that they will be lost.
    void setUnsavedChangesCheck(std::function<bool(const QString &)> check) { hasUnsavedChanges_ = std::move(check); }

    // Git states of the files (absolute paths), for the colors and the
    // Changed Files scope. `inRepository`: the Git submenu is offered.
    void setStatuses(const FileStatusMap &statuses, bool inRepository);
    FileStatusMap statuses() const { return statuses_; }
    // Files Claude has just written: a dot on their icon.
    void setClaudeTouched(const QSet<QString> &paths);
    // Files of the Edited by Claude scope.
    void setClaudeEdited(const QStringList &paths);

    void setScope(Scope scope);
    Scope scope() const { return scope_; }

    // Expands the folders leading to `path` and selects it (once the tree
    // has read it). Returns false, doing nothing, for a path outside the
    // tree or the scope.
    bool select(const QString &path);
    // Absolute path of the current item, empty if none.
    QString currentPath() const;

    // Color of the letters found by the speed search (the theme's).
    void setSearchHighlight(const QColor &color) { searchHighlight_ = color; }

    // Speed search in progress (typed letters), empty if none.
    QString speedSearch() const { return speedSearch_; }

signals:
    void fileActivated(const QString &path);
    void entryRenamed(const QString &oldPath, const QString &newPath);
    void entryDeleted(const QString &path);
    // A file or folder appeared through the tree (created, copied, pasted).
    void entryCreated(const QString &path);
    // Result or error of a tree operation, for the status bar.
    void statusMessage(const QString &message);
    // Context menu commands handled by the window.
    void sendToClaudeRequested(const QString &path);
    // A diagram for Claude to draw, in `folder`.
    void generateDiagramRequested(const QString &folder);
    void openTerminalRequested(const QString &folder);
    void showDiffRequested(const QString &path);
    void stageRequested(const QString &path);
    // Confirmed by the user: restore `path` to its state in the index.
    void rollbackRequested(const QString &path);
    void historyRequested(const QString &path);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    ProjectTreeModel *model_;
    TreeDecorations *proxy_;
    QStringList pendingExpansions_;
    QString rootPath_;
    QString pendingSelection_;
    std::function<bool(const QString &)> hasUnsavedChanges_;
    FileStatusMap statuses_;
    bool inRepository_ = false;
    QStringList claudeEdited_;
    Scope scope_ = Scope::Project;
    QStringList projectExpanded_; // while another scope is shown
    QString speedSearch_;
    QColor searchHighlight_{229, 192, 123};
    QLabel *speedSearchLabel_;
    // Folders pinned at the top while their contents scroll (sticky
    // scroll), outermost first; set at each paint.
    QList<QPersistentModelIndex> stickyRows_;
    QList<QPersistentModelIndex> computeStickyRows() const;
    int stickyRowHeight() const;
    // A fine line under each folder holding `index`, in its branches' `rect`.
    void drawIndentGuides(QPainter *painter, const QRect &rect, const QModelIndex &index) const;

    void collectExpanded(const QModelIndex &parent, QStringList *paths) const;
    void expandPending();
    void showContextMenu(const QPoint &position);
    void addGitMenu(QMenu *menu, const QString &path);
    QString targetFolder(const QModelIndex &index) const;
    QString pathOf(const QModelIndex &index) const;
    void createEntry(const QString &folder, bool isFolder);
    void createDiagramIn(const QString &folder, const QString &kindId);
    // Returns an error message, empty once the entry is created. A new file
    // gets `content`.
    QString createEntryNamed(const QString &folder, const QString &name, bool isFolder,
                             const QByteArray &content = {});
    // Path of the item, empty for none or for the working folder.
    QString editablePath(const QModelIndex &index) const;
    void renameEntry(const QString &path);
    QString renameEntryTo(const QString &path, const QString &name);
    void deleteEntry(const QString &path);
    void confirmRollback(const QString &path);
    void expandRoot();
    // Folder a drop at `position` goes to; empty if none.
    QString dropFolder(const QPoint &position) const;
    // Local files of a drag or of the clipboard.
    static QStringList localPaths(const QMimeData *data);
    // Copies or moves `sources` into `folder`. A single item whose name is
    // taken gets a popup to choose another; several get free names.
    void transfer(const QStringList &sources, const QString &folder, bool move);
    // Returns an error message, empty once done.
    QString transferOne(const QString &source, const QString &destination, bool move);
    void copyPath(const QString &path, bool relative);
    void reveal(const QString &path);
    void selectPending();
    // Files shown by the current scope.
    void updateScopeFiles();
    void expandScope(const QModelIndex &parent);

    // Speed search: the letters typed select the next visible item whose name
    // matches them; Up and Down go to the previous or next one.
    bool handleSpeedSearchKey(QKeyEvent *event);
    void setSpeedSearch(const QString &text);
    bool speedSearchMatches(const QModelIndex &index) const;
    // From `from` (included), in the given direction, without wrapping.
    QModelIndex findMatch(const QModelIndex &from, bool forward) const;
    friend class FileTreeDelegate;
};
