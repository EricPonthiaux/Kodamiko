// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The pointing hand over everything the user clicks, in every window:
// buttons (tool buttons, check boxes, custom ones), combo boxes, and the
// tabs of tab bars (not the empty room beside them). A disabled control
// keeps the arrow. A widget given a cursor of its own before it is first
// shown keeps it.
namespace ClickableCursors {

// Watches the application's widgets; call once, right after the
// QApplication is created and before any widget is.
void install();

} // namespace ClickableCursors
