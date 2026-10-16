#pragma once

#include "Theme.h"

#include <QAbstractButton>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QMenu;
class QMenuBar;
class QToolButton;

// Window control drawn in the theme: minimize, maximize (restore when the
// window is maximized) or close.
class WindowButton : public QAbstractButton {
    Q_OBJECT

public:
    enum class Kind { Minimize, Maximize, Close };

    WindowButton(Kind kind, QWidget *parent);
    void applyTheme(const Theme &theme);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Kind kind_;
    Theme theme_;
    bool hovered_ = false;
};

// Duet's own title bar, in the manner of IntelliJ's main toolbar: the
// menu, the project (with the recent folders), Claude's state, a few actions,
// and the window controls. Dragging its empty parts moves the
// window, a double-click maximizes it.
class TitleBar : public QWidget {
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    QMenuBar *menuBar() const { return menuBar_; }

    void setProject(const QString &name, const QString &toolTip, QMenu *menu);
    void setClaudeState(const QIcon &icon, const QString &text, const QString &toolTip);
    // Buttons at the right, before the window controls, with one of the
    // UiIcons (drawn in the theme's colors).
    void addActionButton(QAction *action, const QString &iconName);

    // Window controls (frameless window); otherwise the system draws them.
    void setWindowControlsVisible(bool visible);
    // Room left free at the left: macOS keeps its traffic lights there.
    void setLeadingSpace(int pixels);
    void applyTheme(const Theme &theme);

    WindowButton *windowButton(WindowButton::Kind kind) const;

signals:
    void claudeClicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QToolButton *addChip();
    void toggleMaximized();
    void updateMaximizeButton();

    Theme theme_;
    QHBoxLayout *layout_;
    QWidget *leadingSpace_;
    QMenuBar *menuBar_;
    QToolButton *project_;
    QToolButton *claude_;
    QHBoxLayout *actions_;
    QList<QPair<QToolButton *, QString>> actionButtons_;
    WindowButton *minimize_;
    WindowButton *maximize_;
    WindowButton *close_;
};
