// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QStatusBar>
#include <QStringList>

class QBoxLayout;
class QFrame;
class QLabel;
class QTabBar;
class QToolButton;

// Keeps workspace modes at the right edge. When the status details no
// longer fit, the same live controls move into an anchored popup.
class WorkspaceStatusBar : public QStatusBar {
    Q_OBJECT

public:
    explicit WorkspaceStatusBar(QTabBar *modes, QWidget *parent = nullptr);

    // Takes ownership of the widgets; later groups stay in the bar longer.
    // Explicit show/hide states survive moving between the bar and popup.
    void addGroup(const QString &title, const QList<QWidget *> &widgets);

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
};
