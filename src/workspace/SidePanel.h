// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/Theme.h"

#include <QIcon>
#include <QStackedWidget>

class QTabBar;
class SideTabBar;

// Tool views at the window's edge, in the manner of KDevelop: a strip of
// vertical tabs (their text read from bottom to top) and the panel they
// show. Clicking the current tab folds the panel away, clicking it again
// (or another tab) brings it back. The strip stays at the edge either way.
class SidePanel : public QStackedWidget {
    Q_OBJECT

public:
    explicit SidePanel(QWidget *parent = nullptr);

    // The strip, the full height of the window's edge, for the caller to
    // place there; its tabs at the top.
    QWidget *strip() const { return strip_; }

    QTabBar *tabBar() const;

    int addView(QWidget *view, const QIcon &icon, const QString &title);
    // Colors of the tabs' labels.
    void applyTheme(const Theme &theme);
    void setViewIcon(QWidget *view, const QIcon &icon);
    // Shows `view`, unfolding the panel.
    void showView(QWidget *view);

    bool isPanelVisible() const { return !isHidden(); }

    void setPanelVisible(bool visible);

signals:
    void panelVisibilityChanged(bool visible);

private:
    QWidget *strip_;
    SideTabBar *tabBar_;
};
