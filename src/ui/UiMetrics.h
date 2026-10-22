// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QSize>

class QAbstractButton;
class QAbstractItemView;
class QWidget;

// Sizes of the interface, taken from IntelliJ's New UI (intellij-community:
// CustomHeader.kt, JBUI.CurrentTheme). IntelliJ gives them for its default
// interface font and multiplies them by JBUI.scale; Duet gives them for its
// own default, 11 pt, and scales them the same way by the application font
// (Preferences ▸ Fonts), so that the bars grow with their text.
namespace UiMetrics {

// The window's title bar, with the menus (HEADER_HEIGHT_NORMAL).
constexpr int titleBarHeight = 40;
// Its chips and buttons (MainToolbar.Button.size).
constexpr int toolbarButtonSize = 30;
// The icon of a chip (the project's folder).
constexpr int chipIconSize = 16;
// The icons of the buttons, tabs, menus and lists (IconSize 16 x 16):
// they grow with the text beside them.
constexpr int iconSize = 16;
// The header of a side panel, its line under it included
// (ToolWindow.Header.height).
constexpr int panelHeaderHeight = 41;

// `referencePx`, given for an 11 pt interface font, at the application
// font's size.
int scaled(int referencePx);
// Gives `widget` the fixed height `referencePx` scaled, and again whenever
// the application font changes.
void setScaledHeight(QWidget *widget, int referencePx);
// The square `referencePx` side scaled: an icon's size.
QSize scaledSquare(int referencePx);
// Gives `widget` the fixed size `referencePx` scaled, and again whenever
// the application font changes.
void setScaledSize(QWidget *widget, QSize referencePx);
// Gives `button`, or the items of `view`, icons of `referencePx` scaled,
// and again whenever the application font changes.
void setScaledIconSize(QAbstractButton *button, int referencePx = iconSize);
void setScaledIconSize(QAbstractItemView *view, int referencePx = iconSize);

} // namespace UiMetrics
