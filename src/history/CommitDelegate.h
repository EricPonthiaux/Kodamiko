// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDateTime>
#include <QPixmap>
#include <QStyledItemDelegate>

class QTreeWidgetItem;
struct CommitSummary;
struct GraphRow;

// Rows of the history on two lines, in the manner of GitHub Desktop: the
// author's avatar (initials on a muted color of their own), the summary
// first, and at the right the branches and tags as small badges; below, the
// author and the date said simply ("2 hours ago"), dimmed, and at the right
// the short ID in monospace, aligned from row to row. With a graph, its
// column comes first, at the left of the avatar.
class CommitDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit CommitDelegate(QObject *parent = nullptr);

    static void setCommit(QTreeWidgetItem *item, const CommitSummary &commit);
    static void setGraphRow(QTreeWidgetItem *item, const GraphRow &row);
    // The lanes of the graph column; 0 hides it.
    void setGraphLanes(int lanes);

    int graphLanes() const { return graphLanes_; }

    // "just now", "5 minutes ago", "yesterday", "3 days ago", then the date.
    static QString relativeDate(const QDateTime &date, const QDateTime &now = QDateTime::currentDateTime());
    // Almost square avatar with the initials of `name`, on a muted color
    // derived from it.
    static QPixmap avatar(const QString &name, int size, qreal ratio);
    static QPixmap drawAvatar(const QString &name, int size, qreal ratio);
    // Color of a branch or tag badge.
    static QColor refColor(const QString &name);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    // The graph of a row in `rect`, the column of its lanes.
    static void paintGraph(QPainter *painter,
                           const QStyleOptionViewItem &option,
                           const GraphRow &row,
                           const QRect &rect);
    // The branches and tags, from the right of `line` leftwards, as long as
    // they leave the summary room. Returns where they start.
    static int paintBadges(QPainter *painter,
                           const QStyleOptionViewItem &option,
                           const QStringList &refs,
                           const QRect &line);

    int graphLanes_ = 0;
};
