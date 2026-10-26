// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

#include <functional>

// The system's own notification center, where Duet can use it: macOS's
// (UNUserNotificationCenter), which needs a bundle identifier and asks the
// user's permission once. Elsewhere, and when it is refused, the caller
// shows its notification through the system tray. One source per system.
namespace NativeNotifications {

// Shows a notification; `clicked` runs, in the main thread, when the user
// clicks it. False if the system's notification center is not available
// (not on this system, not permitted, not a bundle): nothing was shown.
[[nodiscard]] bool show(const QString &title, const QString &message, std::function<void()> clicked);

// Removes this process's notifications from the notification center.
void clear();

} // namespace NativeNotifications
