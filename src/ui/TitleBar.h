#pragma once

#include "ClaudeIndicator.h"
#include "Theme.h"
#include "WindowControls.h"

#include <QIcon>
#include <QPointer>
#include <QWidget>

#include <optional>

class QFrame;
class QHBoxLayout;
class QLabel;
class QMenu;
class QMenuBar;
class QTabBar;
class QToolButton;

// Duet's own title bar, in the manner of IntelliJ's main toolbar: the
// menu, the project (with the recent folders), Claude's state,
// the run block (the configuration and its Run or Stop button, on a tinted
// background), the search actions, and the window controls, in groups set
// apart. Its controls share one height. Dragging its empty parts moves the
// window, a double-click maximizes it.
class TitleBar : public QWidget {
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    QMenuBar *menuBar() const { return menuBar_; }

    // Centers the logo above the side-tab column, following its width as
    // the layout or interface font changes.
    void alignLogoWith(QTabBar *column);
    void setProject(const QString &name, const QString &toolTip, QMenu *menu);
    // Claude's state; hidden while there is no session (`status` empty).
    void setClaudeState(std::optional<ClaudeIndicator::Status> status, const QString &toolTip);
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
    // Room left free at the left: macOS keeps its traffic lights there.
    void setLeadingSpace(int pixels);
    void applyTheme(const Theme &theme);

    WindowButton *windowButton(WindowButton::Kind kind) const;

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void claudeClicked();

protected:
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

    QToolButton *addChip();
    QFrame *createRunBlock();
    // `color` defaults to the theme's foreground.
    QToolButton *actionButton(QAction *action, const QString &iconName, const QColor &color = {});
    // Sizes from UiMetrics, at the application font's size.
    void applyMetrics();
    void sizeActionButton(const ActionButton &entry);
    void updateLogoAlignment();

    Theme theme_;
    QHBoxLayout *layout_;
    QWidget *leadingSpace_;
    QWidget *logo_;
    QPointer<QTabBar> logoColumn_;
    QMenuBar *menuBar_;
    QToolButton *project_;
    ClaudeIndicator *claude_;
    QFrame *runBlock_ = nullptr;
    QToolButton *run_ = nullptr;
    QFrame *runDivider_ = nullptr;
    QHBoxLayout *runActions_ = nullptr;
    QToolButton *runButton_ = nullptr;
    QToolButton *stopButton_ = nullptr;
    bool running_ = false;
    QHBoxLayout *actions_;

    QList<ActionButton> actionButtons_;
    WindowControls *controls_;
};
