// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QMargins>
#include <QSize>

class QAbstractButton;
class QAbstractItemView;
class QTabBar;
class QWidget;

// Sizes of the interface, taken from IntelliJ's New UI (intellij-community:
// CustomHeader.kt, JBUI.CurrentTheme). IntelliJ gives them for its default
// interface font and multiplies them by JBUI.scale; Kodamiko gives them for its
// own default, 11 pt, and scales them the same way by the application font
// (Preferences ▸ Fonts), so that the bars grow with their text.
namespace UiMetrics {

// The window's title bar, with room above and below its menus and controls.
constexpr int titleBarHeight = 52;
// The thin, single-line title bars of the welcome and New Project windows.
constexpr int compactTitleBarHeight = 34;
// Their close button, their content's margins and the spacing between their items.
constexpr QSize compactCloseButtonSize{32, 24};
constexpr QMargins compactTitleBarMargins{16, 0, 5, 0};
constexpr int compactTitleBarSpacing = 6;
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
// The shared space above the editor tabs and the side panel's tab-shaped header.
constexpr int tabTopMargin = 4;

// `referencePx`, given for an 11 pt interface font, at the application
// font's size.
int scaled(int referencePx);
// The height of an editor tab at the application font: a line of text,
// the tab's 7 px paddings and its 2 px accent line (ThemeStyle).
int tabHeight();
// The height of `tabs` as its tabs and style make it. With no tab, that of
// a hidden copy holding one, so that what lines up with the tabs does not
// move when the last one closes.
int tabBarHeight(const QTabBar &tabs);
// The height of the bars' boxes (chips, the run block, buttons, the mode
// tabs): toolbarButtonSize scaled, but never taller than a tab, which
// grows less than the font with it.
int controlHeight();
// Gives `widget` the fixed height controlHeight(), and again whenever the
// application font changes.
void setControlHeight(QWidget *widget);
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
