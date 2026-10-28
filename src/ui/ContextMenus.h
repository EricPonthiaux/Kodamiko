// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>

class QAction;
class QMenu;
class QWidget;

// Context menus placed by Kodamiko rather than by Qt: Qt moves a menu that does
// not fit below the click by the pointer position it last knew and, under
// Wayland, the compositor moves it again, which can open it far above the
// click. Placed inside the application's window, a menu needs neither.
namespace ContextMenus {

// Top-left corner, in the coordinates of `target` and `area`, of a menu of
// `size` opened by a click at `target` inside `area`. On each axis the menu
// goes after the target (below, to the right) when it fits, before it when
// it fits there, and otherwise centred on the target, kept inside the area
// (at its start when the menu is larger than the area).
[[nodiscard]] QPoint placement(QSize size, QPoint target, QRect area);

// Runs `menu` modally for a click at `globalTarget` on `clicked`, placed
// inside the top-level window of the application that holds `clicked`.
// Returns the triggered action, or nullptr when the menu was dismissed.
QAction *exec(QMenu &menu, const QWidget &clicked, QPoint globalTarget);

} // namespace ContextMenus
