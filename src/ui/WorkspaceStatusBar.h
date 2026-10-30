// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QColor>
#include <QStatusBar>
#include <QStringList>

class QBoxLayout;
class QFrame;
class QLabel;
class QPainter;
class QTabBar;
class QToolButton;

// Keeps workspace modes at the right edge. When the status details no
// longer fit, the same live controls move into an anchored popup. In the
// bar, a thin divider stands between two items, as in the title bar.
class WorkspaceStatusBar : public QStatusBar {
    Q_OBJECT

public:
    explicit WorkspaceStatusBar(QTabBar *modes, QWidget *parent = nullptr);

    // Takes ownership of the widgets; later groups stay in the bar longer.
    // Explicit show/hide states survive moving between the bar and popup.
    // A widget made of several (a QBoxLayout) gets the items' spacing, and
    // a divider between its own.
    void addGroup(const QString &title, const QList<QWidget *> &widgets);
    // The theme's separator color.
    void setDividerColor(const QColor &color);

    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Group {
        QWidget *panel;
        QLabel *heading;
        QBoxLayout *row;
        QList<QWidget *> widgets;
        bool inPopup = false;
    };

    void scheduleLayout();
    void updateResponsiveLayout();
    // Returns the room left after the groups kept in the bar.
    int arrangeGroups(int available);
    void widenLabels(int spare);
    void moveGroup(Group &group, bool intoPopup, int index);
    void updateModeLabels();
    void showDetails();
    int expandedModesWidth() const;
    static int boundedWidth(const Group &group);
    // The items shown in the bar, left to right, the modes last.
    QList<const QWidget *> barItems() const;
    void paintDividers(QPainter &painter) const;

    QTabBar *modes_;
    QStringList modeLabels_;
    QWidget *trailing_;
    QBoxLayout *trailingLayout_;
    QWidget *details_;
    QBoxLayout *detailsLayout_;
    QFrame *popup_;
    QToolButton *more_;
    QList<Group> groups_;
    bool compactModes_ = false;
    bool layoutScheduled_ = false;
    QColor dividerColor_;
};
