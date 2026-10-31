// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QEasingCurve>
#include <QObject>

#include <chrono>

class QAbstractAnimation;
class QWidget;

// Material 3 motion for a desktop editor: decelerating entrances, shorter
// accelerating exits, and no overshoot. Focus and input never wait.
namespace Motion {

class Policy : public QObject {
    Q_OBJECT

public:
    explicit Policy(QObject *parent);

    bool isEnabled() const { return enabled_; }

    // Running transitions finish immediately when disabled.
    void setEnabled(bool enabled);

signals:
    void enabledChanged(bool enabled);

private:
    bool enabled_ = true;
};

// Application-owned policy, shared by all windows.
Policy &policy();

constexpr std::chrono::milliseconds hoverDuration{120};
constexpr std::chrono::milliseconds fadeDuration{150};
constexpr std::chrono::milliseconds enterDuration{200};
constexpr std::chrono::milliseconds exitDuration{100};
constexpr std::chrono::milliseconds panelEnterDuration{250};
constexpr std::chrono::milliseconds panelExitDuration{150};

QEasingCurve standardCurve();
QEasingCurve enterCurve();
QEasingCurve exitCurve();

// Honors policy changes while running; connect after the value callbacks.
void track(QAbstractAnimation *animation);

// Watches Qt menus, popup windows, tooltips and widget dialogs. Idempotent.
// Native system dialogs remain under the window system's control.
void install();

// Registers a child popup (which has no Qt::Popup window flag).
void watchPopup(QWidget *widget);

// Whether `widget` can be seen: shown, and its window not minimized. A
// looping animation runs only then: it is updated from showEvent() and
// hideEvent(), which a widget also receives when its window is minimized
// or restored (isVisible() alone stays true in a minimized window).
bool isOnScreen(const QWidget *widget);

// False for foreign graphics effects or surfaces rendered outside QPainter.
bool canAnimate(const QWidget *widget);

// Fades `widget` in from transparent; call it as it is shown. The opacity
// effect it uses is removed at the end. A widget that holds a view drawn
// outside Qt's painter (OpenGL, Qt Quick, a web view), or that has a graphics
// effect of its own, appears at once: an opacity effect would blank it or
// replace its own.
void fadeIn(QWidget *widget);

// Fades a picture of `widget` out where it stands; call it just before
// hiding `widget`. The widget itself is hidden at once, so isVisible() and
// the focus do not wait for the animation; the picture lets the mouse
// through and deletes itself.
void fadeOut(QWidget *widget);

} // namespace Motion
