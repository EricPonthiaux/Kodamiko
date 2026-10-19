#pragma once

#include "Theme.h"

#include <QAbstractButton>
#include <QWidget>

// Window control drawn in the theme: minimize, maximize (restore when the
// window is maximized) or close.
class WindowButton : public QAbstractButton {
    Q_OBJECT

public:
    enum class Kind {
        Minimize,
        Maximize,
        Close
    };

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

// The controls of a frameless window, in the manner of the system's:
// minimize, maximize (restore once maximized) and close, side by side.
class WindowControls : public QWidget {
    Q_OBJECT

public:
    // Controls `parent`'s window, as it is when they are created.
    explicit WindowControls(QWidget *parent);

    WindowButton *button(WindowButton::Kind kind) const;
    void applyTheme(const Theme &theme);
    // Maximizes `window`, or restores it if it is maximized.
    static void toggleMaximized(QWidget *window);

    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateMaximizeButton();

    QWidget *window_;
    WindowButton *minimize_;
    WindowButton *maximize_;
    WindowButton *close_;
};
