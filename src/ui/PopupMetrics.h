// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QMargins>

// Sizes shared by every popup — the panels over the window, the menus, the
// completion, the tooltips — so that they read as one family, as IntelliJ's
// do (JBUI.CurrentTheme.Popup). The corners stay barely rounded, square-cut
// as the editor is. In pixels for an 11 pt interface font, unscaled unless
// passed through UiMetrics::scaled.
namespace PopupMetrics {

// Corners of a popup's frame.
constexpr int frameRadius = 4;
// Corners of a row's selection and hover.
constexpr int rowRadius = 2;
// Corners of a menu item's hover: rounder than a list row's, as a menu's
// items stand apart inside its padding rather than fill a list.
constexpr int menuRowRadius = 4;
// A list row of one line (List.rowHeight); UiMetrics::scaled.
constexpr int rowHeight = 24;
// Room between a row's edge and its icon or text.
constexpr int rowPadding = 8;

// Around the contents of a popup built like a dialog, between its header
// and its footer, and between the blocks of these contents.
constexpr QMargins bodyMargins{16, 14, 16, 14};
constexpr int bodySpacing = 12;
// Around the footer's hint and buttons, and between them.
constexpr QMargins footerMargins{16, 10, 16, 10};
constexpr int footerSpacing = 8;
// The narrowest footer button, so that "OK" and "Cancel" line up.
constexpr int buttonMinWidth = 84;
// The navigation column of a popup in two columns (Preferences), and
// around its contents.
constexpr int sidebarWidth = 200;
constexpr QMargins sidebarMargins{8, 10, 8, 10};

// Around a search popup's field and list (Command Palette, Go To, Find in
// Files), and between them.
constexpr QMargins searchMargins{8, 8, 8, 8};
constexpr int searchSpacing = 6;

} // namespace PopupMetrics
