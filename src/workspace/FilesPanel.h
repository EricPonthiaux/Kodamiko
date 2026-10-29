// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "FileTreeView.h"

#include <QWidget>

class QAction;
class QActionGroup;
class QToolButton;
class ViewHeader;

// Files pane of the side panel: a header above the file tree, as in
// IntelliJ's Project view. The view selector (Project, Changed Files,
// Edited by Claude; Alt+Left and Alt+Right go through them), Select Opened
// File (Alt+F1), Collapse All, and an options menu with Always Select
// Opened File.
class FilesPanel : public QWidget {
    Q_OBJECT

public:
    explicit FilesPanel(QWidget *parent = nullptr);

    FileTreeView *tree() const { return tree_; }

    // The view shown: 0 Project, 1 Changed Files, 2 Edited by the agent.
    int scopeIndex() const;
    void setScopeIndex(int index);

    QToolButton *scopeButton() const { return scope_; }

    bool alwaysSelectOpenedFile() const;
    // The speed search filters the tree (see FileTreeView); applied to the tree.
    void setFilterWhileTyping(bool enabled);
    void setAlwaysSelectOpenedFile(bool enabled);
    // Options menu entries for the tree's layout; applied to the tree.
    void setTreeOptions(const ProjectTreeModel::Options &options);
    // Color of the toolbar's line icons, from the theme.
    void setIconColor(const QColor &color);
    // The agent's name in the scopes ("Edited by Claude"); applied to the tree.
    void setAgentName(const QString &name);

signals:
    // Select Opened File: the window knows which file is open.
    void selectOpenedFileRequested();
    void alwaysSelectOpenedFileChanged(bool enabled);
    void filterWhileTypingChanged(bool enabled);
    void treeOptionsChanged(const ProjectTreeModel::Options &options);

private:
    FileTreeView *tree_;
    void updateScopeText();
    ViewHeader *header_;

    QToolButton *scope_;
    QActionGroup *scopes_;
    QToolButton *select_;
    QToolButton *collapse_;
    QToolButton *options_;
    QAction *alwaysSelect_;
    QAction *filterWhileTyping_;
    QAction *compactFolders_;
    QAction *nestFiles_;
    QAction *showIgnored_;
};
