// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "FileUndoStack.h"
#include "ProjectTreeModel.h"
#include "TreeDecorations.h"
#include "git/FileStatuses.h"

#include <QSet>
#include <QTreeView>

#include <functional>
#include <optional>

class ConfirmPopup;
class LastCommits;
class QLabel;

// Tree of the working folder; a click shows the file in the editor's
// preview tab, a double-click opens it.
// The context menu creates files and folders in the clicked folder, renames
// (F2, Shift+F6) and deletes (Delete) the selected entries, copies their
// path, sends them to the agent, opens a terminal there, and runs git commands
// on them. Several entries are selected with Ctrl and Shift, as in a file
// manager. Ctrl+Z undoes the last file operation (see FileUndoStack).
// The folder itself is the first row, expanded on opening.
// Names are never truncated: when too wide, a horizontal scroll bar
// appears at the bottom.
//
// As in IntelliJ's Project view: names are colored by their git state
// (folders too, from the files they contain), typing a few letters starts a
// speed search, and a scope restricts the tree to the changed files or to
// those edited by the agent. A dot on the icon marks the files the agent has
// just written.
//
// As in VS Code: the files with errors or warnings, and the folders holding
// them, are colored by their worst problem, with their count at the row's
// end; a dot left of a file marks it open in the editor; the speed search
// can filter the tree instead of jumping from match to match.
class FileTreeView : public QTreeView {
    Q_OBJECT

public:
    enum class Scope {
        Project,
        ChangedFiles,
        EditedByClaude
    };

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
    // Ask for a new name for the current item, or for confirmation for the
    // selected ones (never the working folder itself). Deleting moves to the
    // trash when possible.
    void renameCurrent();
    void deleteSelection();

    // Clipboard, as in a file manager: Ctrl+X, Ctrl+C, Ctrl+V, on the
    // selected entries. Pasting cut entries moves them, otherwise copies
    // (files copied in a file manager too). A name already taken by a
    // single entry is asked for again in a popup.
    void cutSelection();
    void copySelection();
    void paste();
    // Ctrl+Z: reverts the last creation, copy, rename, move or deletion to
    // the trash done in the tree.
    void undo();
    // F5: copy of the current item in the same folder, under a new name.
    void duplicateCurrent();

    // Whether files open under a path have unsaved changes: renaming is
    // then refused, and deleting warns that they will be lost.
    void setUnsavedChangesCheck(std::function<bool(const QString &)> check) { hasUnsavedChanges_ = std::move(check); }

    // Git states of the files (absolute paths), for the colors and the
    // Changed Files scope. `inRepository`: the Git submenu is offered.
    void setStatuses(const FileStatusMap &statuses, bool inRepository);

    FileStatusMap statuses() const { return statuses_; }

    // The agent's name in the menus and tooltips ("Send to Claude").
    void setAgentName(const QString &name);

    // Whether the agent draws diagrams: "Drawn by …" in the context menu.
    void setCanDrawDiagrams(bool canDraw) { canDrawDiagrams_ = canDraw; }

    // Files the agent has just written: a dot on their icon.
    void setClaudeTouched(const QSet<QString> &paths);
    // Errors and warnings of the files (absolute paths).
    void setProblems(const ProblemCounts &problems);
    // Files open in the editor: a dot left of them.
    void setOpenFiles(const QSet<QString> &paths);
    // Files of the Edited by the agent scope.
    void setClaudeEdited(const QStringList &paths);

    void setScope(Scope scope);

    Scope scope() const { return scope_; }

    // Expands the folders leading to `path` and selects it (once the tree
    // has read it). Returns false, doing nothing, for a path outside the
    // tree or the scope.
    bool select(const QString &path);
    // Absolute path of the current item, empty if none.
    QString currentPath() const;
    // Absolute paths of the selected entries, without those inside a
    // selected folder; the current item when nothing is selected.
    QStringList selectedPaths() const;

    // Color of the letters found by the speed search (the theme's).
    void setSearchHighlight(const QColor &color) { searchHighlight_ = color; }

    // Speed search in progress (typed letters), empty if none.
    QString speedSearch() const { return speedSearch_; }

    // The speed search hides the rows that do not match (among the folders
    // read so far), rather than jumping from match to match.
    void setFilterWhileTyping(bool enabled);

    bool filterWhileTyping() const { return filterWhileTyping_; }

signals:
    void fileActivated(const QString &path);
    // A single click on a file: shown in the editor's preview tab.
    void filePreviewRequested(const QString &path);
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
    bool viewportEvent(QEvent *event) override;

private:
    ProjectTreeModel *model_;
    TreeDecorations *proxy_;
    QString agentName_;
    bool canDrawDiagrams_ = false;
    LastCommits *lastCommits_;
    FileUndoStack undoStack_;
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
    bool filterWhileTyping_ = false;
    // The folders expanded when the filter started; nullopt when not filtering.
    std::optional<QStringList> filterExpanded_;
    QColor searchHighlight_{229, 192, 123};
    QLabel *speedSearchLabel_;
    // Folders pinned at the top while their contents scroll (sticky
    // scroll), outermost first; set at each paint.
    QList<QPersistentModelIndex> stickyRows_;
    QList<QPersistentModelIndex> computeStickyRows() const;
    int stickyRowHeight() const;
    // A fine line under each folder holding `index`, in its branches' `rect`.
    void drawIndentGuides(QPainter *painter, const QRect &rect, const QModelIndex &index) const;
    // A dot left of a file open in the editor, in its branches' `rect`.
    void drawOpenMark(QPainter *painter, const QRect &rect, const QModelIndex &index) const;
    // Folder whose guide is brighter: the current item when it is an
    // expanded folder, else its parent.
    QModelIndex currentFolder() const;

    void collectExpanded(const QModelIndex &parent, QStringList *paths) const;
    void expandPending();
    void showContextMenu(const QPoint &position);
    void addEntryActions(QMenu *menu, const QModelIndex &index);
    // Git commands on `path`; Add on every entry of `selection`.
    void addGitMenu(QMenu *menu, const QString &path, const QStringList &selection);
    QString targetFolder(const QModelIndex &index) const;
    QString pathOf(const QModelIndex &index) const;
    void createEntry(const QString &folder, bool isFolder);
    void createDiagramIn(const QString &folder, const QString &kindId);
    // Returns an error message, empty once the entry is created. A new file
    // gets `content`.
    QString createEntryNamed(const QString &folder, const QString &name, bool isFolder, const QByteArray &content = {});
    // Path of the item, empty for none or for the working folder.
    QString editablePath(const QModelIndex &index) const;
    // selectedPaths() but the working folder.
    QStringList editableSelection() const;
    void renameEntry(const QString &path);
    QString renameEntryTo(const QString &path, const QString &name);
    void deleteEntry(const QString &path);
    void deleteEntries(const QStringList &paths);
    // Both emit entryDeleted once done; false otherwise (an error shown in
    // `confirm` for the permanent deletion).
    bool moveToTrash(const QString &path);
    bool deletePermanently(ConfirmPopup *confirm, const QString &path, bool isFolder);
    // The editor follows an undone step, and the tree shows its result.
    void reportUndone(const FileUndoStack::Step &step);
    // The entries, as a file manager puts them on the clipboard.
    void putOnClipboard(const QStringList &paths, bool cut);
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
    void transferSingle(const QString &source, const QString &folder, bool move);
    void askTransferName(const QString &source, const QString &folder, bool move);
    void copyPath(const QString &path, bool relative);
    void reveal(const QString &path);
    void selectPending();
    // Files shown by the current scope.
    void updateScopeFiles();
    // Paths the current scope would show, before checking the tree.
    QStringList scopeCandidates() const;
    // Row of `path` in the model, invalid outside the working folder.
    QModelIndex rowInTree(const QString &path) const;
    void expandScope(const QModelIndex &parent);

    // Speed search: the letters typed select the next visible item whose name
    // matches them; Up and Down go to the previous or next one.
    bool handleSpeedSearchKey(QKeyEvent *event);
    // Keys acting on a search in progress: whether the key is consumed;
    // nullopt for the keys that do not act on it.
    std::optional<bool> handleActiveSearchKey(QKeyEvent *event);
    // Up or Down during a search: the previous or next match.
    void stepSpeedSearch(bool forward);
    // Filter while typing: the tree shows the matches of `text` only, then
    // comes back as it was once the search ends.
    void applyNameFilter(const QString &text);
    // Asks for the last commit of the file under the mouse, for its tooltip.
    void requestLastCommit(const QPoint &position);
    void showLastCommit(const QString &path, const QString &description);
    // Tree shortcuts that copy a path or send it to the agent.
    bool handlePathKey(QKeyEvent *event);
    // Shortcuts acting on the current entry: clipboard, duplicate, delete, rename.
    bool handleEntryKey(QKeyEvent *event);
    void setSpeedSearch(const QString &text);
    bool speedSearchMatches(const QModelIndex &index) const;
    // From `from` (included), in the given direction, without wrapping.
    QModelIndex findMatch(const QModelIndex &from, bool forward) const;
    friend class FileTreeDelegate;
};
