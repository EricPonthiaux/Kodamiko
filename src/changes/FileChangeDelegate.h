#pragma once

#include "git/FileChange.h"

#include <QStyledItemDelegate>

class QTreeWidgetItem;
struct FilePatch;

// Rows of changed files, as in IntelliJ's commit window: the file's icon, its
// name, its folder dimmed after it, and a colored badge for the change
// (M, A, D, R, U) at the right, the lines added and removed before it.
// Section titles ("Staged", "Unstaged") are small capitals with their
// count. Rows are given room to breathe.
class FileChangeDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    enum Role {
        PathRole = Qt::UserRole + 50, // path shown (relative), "/" separators
        KindRole,                     // ChangeKind
        OldPathRole,                  // renamed from
        SectionCountRole,             // on a section title: its number of files
        AddedRole,                    // lines added, when known
        RemovedRole,                  // lines removed
    };

    explicit FileChangeDelegate(QObject *parent = nullptr);

    // Fills `item` for `change`; its text stays the path (accessibility).
    static void setChange(QTreeWidgetItem *item, const FileChange &change);
    // Shows the lines `patch` adds and removes ("+12 −3").
    static void setStats(QTreeWidgetItem *item, const FilePatch &patch);
    // Makes `item` a section title (still selectable: it stands for its files).
    static void setSection(QTreeWidgetItem *item, const QString &title, int count);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};
