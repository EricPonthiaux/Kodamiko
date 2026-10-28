// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Theme.h"
#include "WindowControls.h"

#include <QIcon>
#include <QPointer>
#include <QWidget>

#include <initializer_list>

class BrandLogo;
class ElidingToolButton;
class QFrame;
class QHBoxLayout;
class QLabel;
class QMenu;
class QMenuBar;
class QSpacerItem;
class QTabBar;
class QToolButton;

// Kodamiko's own title bar, in the manner of IntelliJ's main toolbar: the menu
// at the left; in the middle, the project (with the recent folders) and the
// run block (the configuration and its Run or Stop button, on a tinted
// background); at the right, the search actions, then a divider and the
// window controls. The middle group stays centered on the bar while the room
// allows it. Its controls share one height. Dragging its empty parts moves
// the window, a double-click maximizes it.
class TitleBar : public QWidget {
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    QMenuBar *menuBar() const { return menuBar_; }

    // Centers the logo above the side-tab column, following its width as
    // the layout or interface font changes.
    void alignLogoWith(QTabBar *column);
    void setProject(const QString &name, const QString &toolTip, QMenu *menu);
    // Buttons at the right, before the window controls, with one of the
    // UiIcons (drawn in the theme's colors).
    void addActionButton(QAction *action, const QString &iconName);

    // The run block: the configuration (its menu chooses one), then one
    // button in the same place: Run while the configuration is stopped, Stop
    // while it runs. Hidden while `name` is empty.
    struct RunChip {
        QIcon icon;
        QString name;
        QString toolTip;
        QMenu *menu = nullptr; // not owned
        bool running = false;
    };

    void setRunConfiguration(const RunChip &chip);
    void setRunActions(QAction *run, QAction *stop);

    // Window controls (frameless window); otherwise the system draws them.
    void setWindowControlsVisible(bool visible);
    // Room left free at the left: macOS keeps its traffic lights there, in
    // the logo's place, which the bar then hides.
    void setLeadingSpace(int pixels);
    // Room left free at the right, after the last button.
    void setTrailingSpace(int pixels);
    // The logo's mark at `scale` (0 to 1) of its place, which keeps its size.
    void setLogoScale(qreal scale);
    void applyTheme(const Theme &theme);

    WindowButton *windowButton(WindowButton::Kind kind) const;

    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    struct ActionButton {
        QToolButton *button;
        QString iconName;
        QColor color;
        int inset = 0; // smaller by this, inside the run block
    };

    ElidingToolButton *addChip();
    QFrame *createRunBlock();
    // `color` defaults to the theme's foreground.
    QToolButton *actionButton(QAction *action, const QString &iconName, const QColor &color = {});
    // Sizes from UiMetrics, at the application font's size.
    void applyMetrics();
    void sizeActionButton(const ActionButton &entry);
    void updateLogoAlignment();
    // Ends the logo so that the first menu title's text follows the mark as
    // each title's text follows the one before it.
    void fitLogoToMenuTitles();
    void updateResponsiveLayout();
    // Sizes the balances so that the project and the run block sit in the
    // middle of the bar, as far as the room left allows.
    void centerProjectGroup();
    // The width of the visible `widgets`, each with the gap after it.
    int sideWidth(std::initializer_list<const QWidget *> widgets) const;
    // The run block's width with its configuration at its bounded width.
    int boundedRunBlockWidth() const;
    void rebuildCompactMenu();
    void scheduleLayout();

    Theme theme_;
    QHBoxLayout *layout_;
    QSpacerItem *leftBalance_;  // owned by layout_
    QSpacerItem *rightBalance_; // owned by layout_
    BrandLogo *logo_;
    QPointer<QTabBar> logoColumn_;
    QMenuBar *menuBar_;
    QToolButton *menuButton_;
    QMenu *compactMenu_;
    ElidingToolButton *project_;
    QFrame *runBlock_ = nullptr;
    ElidingToolButton *run_ = nullptr;
    QFrame *runDivider_ = nullptr;
    QHBoxLayout *runActions_ = nullptr;
    QToolButton *runButton_ = nullptr;
    QToolButton *stopButton_ = nullptr;
    bool running_ = false;
    QHBoxLayout *actions_;
    QWidget *actionGroup_;
    QList<QAction *> secondaryActions_;
    bool layoutScheduled_ = false;

    QList<ActionButton> actionButtons_;
    QFrame *controlsDivider_;
    WindowControls *controls_;
};
