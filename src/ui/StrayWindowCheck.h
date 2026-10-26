// SPDX-License-Identifier: GPL-3.0-only

#pragma once

class QWidget;

// Warns (qWarning) when a top-level Qt::Window that Duet did not mean to
// open is shown: usually a widget shown before it was given a parent. On
// macOS such a window leaves a full-screen Space for the desktop. Dialogs,
// popups, menus and tool tips are not concerned.
namespace StrayWindowCheck {

// Watches the application's windows; call once, after the QApplication is
// created.
void install();

// Marks `window` as one of Duet's own top-level windows (a main window,
// the welcome window, a detached page), which the check leaves alone.
void allow(QWidget *window);

} // namespace StrayWindowCheck
