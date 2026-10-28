// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>

#include <memory>

class QAction;
class QMenu;
class QMenuBar;
class QString;
class QWidget;

// What the desktop offers Kodamiko's windows beyond Qt, on the systems that
// have it; elsewhere each function does nothing. One source per system.
namespace DesktopIntegration {

// Gives the process the identity the taskbar groups its windows under and
// notifications come from (Windows' AppUserModelID, "Kodamiko.Kodamiko"), the same
// for every Kodamiko process and for the installer's shortcut. Call it at
// start, before the first window is shown.
void identifyApplication();

// Makes `window`, a top-level window without parent, open on the current
// Space, beside a full-screen window, instead of switching to the desktop
// (macOS). Call it before the window is first shown (it creates the native
// window) and again after every change of its window flags, which
// recreates the native window.
void followActiveSpace(QWidget *window);

// Makes `menu` the menu of the application's icon in the Dock (macOS),
// above the system's own items. Not owned: it must outlive its use there.
void setDockMenu(QMenu *menu);

// The system's menu bar where it is separate from the windows (macOS),
// holding `actions`: those with a menu role (Preferences, Quit) go to the
// application menu, where the system expects them. Null elsewhere, where
// the window's own menu bar is enough. The actions are not owned.
std::unique_ptr<QMenuBar> createNativeMenuBar(const QList<QAction *> &actions);

// Keeps the system from slowing the process down while it is in the
// background (macOS's App Nap throttles its timers and its sockets): for
// work the user waits for, such as an agent's. `reason` says which, for
// the system's tools. Several begins need a single end.
void beginBackgroundActivity(const QString &reason);
void endBackgroundActivity();

} // namespace DesktopIntegration
