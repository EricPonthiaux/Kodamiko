#pragma once

#include <QDateTime>
#include <QPixmap>
#include <QStyledItemDelegate>

class QTreeWidgetItem;
struct CommitSummary;

// Rows of the history on two lines, in the manner of GitHub Desktop: the
// author's avatar (initials on a color of their own), the branches and tags
// as pills and the summary, then the author, the date said simply ("2 hours
// ago") and the short ID, dimmed.
class CommitDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    enum Role { AuthorRole = Qt::UserRole + 60, DateRole, ShortIdRole, RefsRole };

    explicit CommitDelegate(QObject *parent = nullptr);

    static void setCommit(QTreeWidgetItem *item, const CommitSummary &commit);

    // "just now", "5 minutes ago", "yesterday", "3 days ago", then the date.
    static QString relativeDate(const QDateTime &date, const QDateTime &now = QDateTime::currentDateTime());
    // Round avatar with the initials of `name`, on a color derived from it.
    static QPixmap avatar(const QString &name, int size, qreal ratio);
    // Color of a branch or tag pill.
    static QColor refColor(const QString &name);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};
