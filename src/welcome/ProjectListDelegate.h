// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/Theme.h"

#include <QStyledItemDelegate>

#include <functional>

class QDate;
class QDateTime;

// Paints the welcome window's list of projects: each project with its
// folder icon, its name, its path, when it was opened last
// and a ⋯ button for its menu, shown on the row under the mouse or
// selected; a project whose folder is gone gets a Locate… button. The
// list can also hold the headings of its groups (headingRole).
class ProjectListDelegate : public QStyledItemDelegate {
public:
    // A project's ProjectConfig.
    static constexpr int projectRole = Qt::UserRole;
    // True when the project's folder no longer exists.
    static constexpr int missingRole = Qt::UserRole + 1;
    // True for a group's heading, which is not a project.
    static constexpr int headingRole = Qt::UserRole + 2;

    // What the row's buttons do, called on a click.
    struct Actions {
        std::function<void(const QModelIndex &project, QPoint globalPosition)> showMenu;
        std::function<void(const QModelIndex &project)> locate;
    };

    ProjectListDelegate(Theme theme, Actions actions, QObject *parent);

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

protected:
    bool editorEvent(QEvent *event,
                     QAbstractItemModel *model,
                     const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;

private:
    void paintHeading(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;
    void paintProject(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;

    Theme theme_;
    Actions actions_;
};

namespace LastOpened {

// When a project was opened, as the list shows it: "Today", "Yesterday",
// "3 days ago", "Last week", "2 weeks ago", then "Mar 2026"; empty when
// `opened` is not valid.
QString describe(const QDateTime &opened, const QDate &today);

} // namespace LastOpened
