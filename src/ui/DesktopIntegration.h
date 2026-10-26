// SPDX-License-Identifier: GPL-3.0-only

#pragma once

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

} // namespace DesktopIntegration
