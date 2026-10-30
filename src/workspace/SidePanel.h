// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/Theme.h"

#include <QIcon>
#include <QStackedWidget>

class QSplitter;
class QTabBar;
class SideTabBar;

// Tool views at the window's edge, in the manner of KDevelop: a strip of
// vertical tabs (their text read from bottom to top) and the panel they
// show. Clicking the current tab folds the panel away, clicking it again
// (or another tab) brings it back. The strip stays at the edge either way.
// On a short window, the strip scrolls its tabs instead of making the
// window taller. At the window's right edge, the same, mirrored.
class SidePanel : public QStackedWidget {
    Q_OBJECT

public:
    enum class Edge {
        Left,
        Right
    };

    explicit SidePanel(QWidget *parent = nullptr);

    // The strip, the full height of the window's edge, for the caller to
    // place there; its tabs at the top.
    QWidget *strip() const { return strip_; }

    QTabBar *tabBar() const;

    Edge edge() const { return edge_; }

    // The window's edge the caller places the strip at: its tabs face the
    // panel, their text read from top to bottom at the right edge.
    void setEdge(Edge edge);

    int addView(QWidget *view, const QIcon &icon, const QString &title);
    // The headers' height and the vertical strip's width follow the editor tabs' height.
    void alignHeadersWith(QTabBar *tabs);
    // The tabs and their separator; the open tab shares the panel's background.
    void applyTheme(const Theme &theme);
    void setViewIcon(QWidget *view, const QIcon &icon);
    // Shows `view`, unfolding the panel.
    void showView(QWidget *view);

    bool isPanelVisible() const { return !isHidden(); }

    void setPanelVisible(bool visible);

    // Lets the user fold the panel by dragging the handle of `splitter`,
    // which holds it, to the strip. Not owned: the splitter is its parent.
    void foldWithSplitter(QSplitter *splitter);

signals:
    void panelVisibilityChanged(bool visible);

private:
    void restoreCollapsedWidth();

    QWidget *strip_;
    SideTabBar *tabBar_;
    QSplitter *splitter_ = nullptr;
    Edge edge_ = Edge::Left;
};
