#pragma once

#include "FileTreeView.h"

#include <QWidget>

class QAction;
class QComboBox;
class QToolButton;

// Files pane of the side panel: a toolbar above the file tree, as in
// IntelliJ's Project view. The scope (Project, Changed Files, Edited by
// Claude), Select Opened File (Alt+F1), Collapse All, and an options menu
// with Always Select Opened File.
class FilesPanel : public QWidget {
    Q_OBJECT

public:
    explicit FilesPanel(QWidget *parent = nullptr);

    FileTreeView *tree() const { return tree_; }
    QComboBox *scopeBox() const { return scope_; }
    bool alwaysSelectOpenedFile() const;
    void setAlwaysSelectOpenedFile(bool enabled);
    // Options menu entries for the tree's layout; applied to the tree.
    void setTreeOptions(const ProjectTreeModel::Options &options);
    // Color of the toolbar's line icons, from the theme.
    void setIconColor(const QColor &color);

signals:
    // Select Opened File: the window knows which file is open.
    void selectOpenedFileRequested();
    void alwaysSelectOpenedFileChanged(bool enabled);
    void treeOptionsChanged(const ProjectTreeModel::Options &options);

private:
    FileTreeView *tree_;
    QComboBox *scope_;
    QToolButton *select_;
    QToolButton *collapse_;
    QToolButton *options_;
    QAction *alwaysSelect_;
    QAction *compactFolders_;
    QAction *nestFiles_;
    QAction *showIgnored_;
};
