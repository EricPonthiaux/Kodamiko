// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/FileChange.h"

#include <QStyledItemDelegate>

class QTreeWidgetItem;
struct FilePatch;

// Rows of changed files, as in IntelliJ's commit window: the file's icon, its
// name, its folder dimmed after it, and a colored badge for the change
// (M, A, D, R, U) at the right, the lines added and removed before it.
// In a tree (see ChangeTree), folder rows show the folder's icon and
// name, and the files under them leave their folder out. Rows are given
// room to breathe. A row with a check state (Qt::CheckStateRole) has a check
// box at its left; a click on it checks the row, or unchecks it when it was
// checked, through the model (QTreeWidget::itemChanged()).
class FileChangeDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    // Item data roles; tests/tst_changes.cpp reads them too.
    // Item data roles of the file rows.
    static constexpr int pathRole = Qt::UserRole + 50; // path shown (relative), "/" separators
    static constexpr int kindRole = pathRole + 1;      // ChangeKind
    static constexpr int oldPathRole = pathRole + 2;   // renamed from
    static constexpr int addedRole = pathRole + 4;     // lines added, when known
    static constexpr int removedRole = pathRole + 5;   // lines removed
    static constexpr int folderRole = pathRole + 6;    // on a folder row: its path, "/" separators

    explicit FileChangeDelegate(QObject *parent = nullptr);

    // Fills `item` for `change`; its text stays the path (accessibility).
    static void setChange(QTreeWidgetItem *item, const FileChange &change);
    // Shows the lines `patch` adds and removes ("+12 −3").
    static void setStats(QTreeWidgetItem *item, const FilePatch &patch);
    static void setStats(QTreeWidgetItem *item, int added, int removed);
    // Makes `item` the row of the folder at `path`, shown as `label` (the
    // part of the path below its parent row).
    static void setFolder(QTreeWidgetItem *item, const QString &label, const QString &path);
    static bool isFolder(const QTreeWidgetItem *item);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

protected:
    bool editorEvent(QEvent *event,
                     QAbstractItemModel *model,
                     const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;
};
