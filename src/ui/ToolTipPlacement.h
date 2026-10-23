// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Keeps the tooltips the mouse brings up beside the mouse, in every window.
// Qt flips a tooltip that would overflow the screen's right edge to the
// left of the mouse: a wide one (a file's path, a branch's details) then
// lands far from what it describes. Here it slides left only as far as the
// screen requires. Tooltips shown by code (the editor's hover help, the
// documentation at the caret) keep the position Qt gives them.
namespace ToolTipPlacement {

// Watches the application's tooltips; call once, after the QApplication is
// created.
void install();

} // namespace ToolTipPlacement
