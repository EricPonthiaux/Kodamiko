// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QFont>
#include <QObject>

class QTreeView;

// The zoom of a tree, as in the editors: Ctrl+wheel over it enlarges or
// shrinks its font by one point per notch, its rows and icons with it
// (TreeMetrics). Each tree has its own, kept while the window lives,
// through the changes of the trees' font (Preferences ▸ Fonts).
class TreeZoom : public QObject {
    Q_OBJECT

public:
    // Gives `font`, zoomed, to `tree`; the first call makes it zoomable.
    // The zoom belongs to the tree, which owns it.
    static void setFont(QTreeView *tree, const QFont &font);

    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit TreeZoom(QTreeView *tree);

    void apply();

    QTreeView *tree_;
    QFont baseFont_;
    int zoomPoints_ = 0; // added to the base font's size
};
