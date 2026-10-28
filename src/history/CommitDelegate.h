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
// column comes first, at the left of the avatar. The first row of a day
// carries the day's heading above it ("Today", "Yesterday", "Thursday"),
// the graph's lines running through it; a commit made with Claude (its
// author, or a Co-Authored-By: Claude trailer) has Claude's mark before its
// ID. An unfolded row (setUnfolded()) shows the message's body under its
// two lines, the graph's lines running through it too.
class CommitDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit CommitDelegate(QObject *parent = nullptr);

    static void setCommit(QTreeWidgetItem *item, const CommitSummary &commit);
    static void setGraphRow(QTreeWidgetItem *item, const GraphRow &row);
    // The heading of the day `item` starts, above it; empty: none.
    static void setDayHeading(QTreeWidgetItem *item, const QString &heading);
    // Unfolded, the row shows the commit's message under its two lines
    // (its body, then the email and the full date). Folded at first.
    static void setUnfolded(QTreeWidgetItem *item, bool unfolded);
    static bool isUnfolded(const QTreeWidgetItem *item);
    // The commit's day, for setDayHeading().
    static QDate day(const QTreeWidgetItem *item);
    // The lanes of the graph column; 0 hides it.
    void setGraphLanes(int lanes);

    int graphLanes() const { return graphLanes_; }

    // "Today", "Yesterday", the day of the week within a week, then the
    // date ("Thursday, October 22"), its year when it is not `today`'s.
    static QString dayHeading(QDate day, QDate today = QDate::currentDate());
    // Whether Claude made `commit`: its author, or a co-author.
    static bool isMadeWithClaude(const CommitSummary &commit);
    // Whether Claude made the commit of `item`, as setCommit() read it.
    static bool isMadeWithClaude(const QTreeWidgetItem *item);
    // Claude's own orange: its mark, its avatar, the ring of its commits' dots.
    static QColor claudeColor();
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
    // Where a row's graph goes: the column of its lanes, and the height of
    // its dot (lower than the middle under a day's heading); whether its dot
    // is ringed in Claude's color.
    struct GraphArea {
        QRect column;
        qreal dotY = 0;
        bool isMadeWithClaude = false;
    };

    static void paintGraph(QPainter *painter,
                           const QStyleOptionViewItem &option,
                           const GraphRow &row,
                           const GraphArea &area);
    // A day's heading in `rect`, from `left`: its name, then a rule.
    static void paintDayHeading(QPainter *painter,
                                const QStyleOptionViewItem &option,
                                const QString &heading,
                                QRect rect);
    // The height of the summary's line and the details' line.
    static int twoLinesHeight(const QFontMetrics &metrics);
    // Where the text starts in a row, from its left: after the graph and the avatar.
    int textLeft(const QModelIndex &index) const;
    // An unfolded row's message, wrapped at `width`: its height.
    static int messageHeight(const QStyleOptionViewItem &option, const QModelIndex &index, int width);
    // The body, then the email and the full date, in `area`.
    static void paintMessage(QPainter *painter,
                             const QStyleOptionViewItem &option,
                             const QModelIndex &index,
                             QRect area);
    // The second line: author · time at the left, Claude's mark and the ID
    // at the right.
    static void paintDetails(QPainter *painter,
                             const QStyleOptionViewItem &option,
                             const QModelIndex &index,
                             const QRect &line);
    // The branches and tags, from the right of `line` leftwards, as long as
    // they leave the summary room. Returns where they start.
    static int paintBadges(QPainter *painter,
                           const QStyleOptionViewItem &option,
                           const QStringList &refs,
                           const QRect &line);

    int graphLanes_ = 0;
};
