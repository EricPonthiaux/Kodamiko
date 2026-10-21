// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Usage.h"

#include <QList>
#include <QSet>
#include <QWidget>

class QAction;
class QLabel;
class QMenu;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class TreeColumnFit;
class ViewHeader;

// Usages pane, as IntelliJ's Find tool window: the usages a Find Usages
// found, by file, each with its line. The filter menu leaves kinds of
// usages out (reads, writes, calls…); Rerun searches again; the history
// shows the usages of an earlier search. A click, or goToNext() and
// goToPrevious(), opens a usage.
class UsagesView : public QWidget {
    Q_OBJECT

public:
    explicit UsagesView(QWidget *parent = nullptr);

    // Folders are shown relative to it; forgets the history.
    void setProjectFolder(const QString &folder);
    void setDarkIcons(bool dark);
    // Color of the header's line icons, from the theme.
    void setIconColor(const QColor &color);

    // Shows `results`, the latest of the history.
    void showResults(const UsageResults &results);
    // The usage after (before) the selected one, opened; from the last
    // one, the first.
    void goToNext();
    void goToPrevious();

    QTreeWidget *tree() const { return tree_; }

    // The filter of `kind`, checked while its usages are shown.
    QAction *filterAction(UsageKind kind) const;

    // The searches shown, latest first.
    const QList<UsageResults> &history() const { return history_; }

signals:
    // `line` and `column` from 0.
    void usageActivated(const QString &path, int line, int column);
    void rerunRequested(const UsageSearch &search);

private:
    void createFilters();
    void showHistoryMenu();
    void rebuild();
    void addFile(const QString &path, const QList<Usage> &usages);
    void activate(QTreeWidgetItem *item);
    // The usage `step` rows away from the selected one, among the shown.
    void goBy(int step);
    QList<QTreeWidgetItem *> usageItems() const;
    // "12 usages of “total”"…
    QString describe(const UsageResults &results, int shown) const;

    QString folder_;
    bool darkIcons_ = true;
    QList<UsageResults> history_;
    int current_ = -1; // in history_
    QSet<UsageKind> hiddenKinds_;
    QList<std::pair<UsageKind, QAction *>> filters_;

    ViewHeader *header_;
    QMenu *filterMenu_;
    QMenu *historyMenu_;
    QTreeWidget *tree_;
    TreeColumnFit *columnFit_;
    QLabel *placeholder_;
    QStackedWidget *stack_;
};
