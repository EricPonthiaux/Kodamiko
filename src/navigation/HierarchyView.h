// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Hierarchy.h"

#include <QWidget>

#include <vector>

class QLabel;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class TreeColumnFit;
class ViewHeader;

// Hierarchy pane, as IntelliJ's Call Hierarchy and Type Hierarchy: the
// element at the cursor, and under it its callers (supertypes), or what it
// calls (its subtypes), unfolded one level at a time. A click opens an
// element where it is declared.
class HierarchyView : public QWidget {
    Q_OBJECT

public:
    explicit HierarchyView(QWidget *parent = nullptr);

    void setIconColor(const QColor &color);

    // Shows `hierarchy` upwards, its roots unfolded.
    void showHierarchy(Hierarchy hierarchy);
    void setDirection(Hierarchy::Direction direction);
    // Back to the placeholder, as before the first hierarchy.
    void clear();

    Hierarchy::Direction direction() const { return direction_; }

    QTreeWidget *tree() const { return tree_; }

signals:
    // `line` and `column` from 0.
    void elementActivated(const QString &path, int line, int column);

private:
    void rebuild();
    QTreeWidgetItem *addItem(QTreeWidgetItem *parent, const LspProtocol::HierarchyItem &element);
    // Asks for the children of `item` the first time it is unfolded.
    void load(QTreeWidgetItem *item);
    void fill(int generation, int element, const QList<LspProtocol::HierarchyItem> &children);
    void activate(const QTreeWidgetItem *item);
    void updateButtons();

    Hierarchy hierarchy_;
    Hierarchy::Direction direction_ = Hierarchy::Direction::Up;
    // The elements shown, by the number their tree item holds; a new tree
    // is a new generation, and late answers for the old one are dropped.
    std::vector<LspProtocol::HierarchyItem> elements_;
    std::vector<QTreeWidgetItem *> elementItems_;
    int generation_ = 0;

    ViewHeader *header_;
    QToolButton *up_;
    QToolButton *down_;
    QTreeWidget *tree_;
    TreeColumnFit *columnFit_;
    QLabel *placeholder_;
    QStackedWidget *stack_;
};
