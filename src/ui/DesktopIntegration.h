// SPDX-License-Identifier: GPL-3.0-only

#pragma once

class QMenu;
class QString;
class QWidget;

// What the desktop offers Duet's windows beyond Qt, on the systems that
// have it; elsewhere each function does nothing. One source per system.
namespace DesktopIntegration {

// Makes `window`, a top-level window without parent, open on the current
// Space, beside a full-screen window, instead of switching to the desktop
// (macOS). Call it before the window is first shown (it creates the native
// window) and again after every change of its window flags, which
// recreates the native window.
void followActiveSpace(QWidget *window);

// Makes `menu` the menu of the application's icon in the Dock (macOS),
// above the system's own items. Not owned: it must outlive its use there.
void setDockMenu(QMenu *menu);

// Keeps the system from slowing the process down while it is in the
// background (macOS's App Nap throttles its timers and its sockets): for
// work the user waits for, such as an agent's. `reason` says which, for
// the system's tools. Several begins need a single end.
void beginBackgroundActivity(const QString &reason);
void endBackgroundActivity();

} // namespace DesktopIntegration
