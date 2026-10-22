// SPDX-License-Identifier: GPL-3.0-only

#include "ContextMenus.h"

#include <QMenu>
#include <QWidget>

#include <algorithm>

namespace {

// Start of a menu of `length` opened at `target` on an axis that runs from
// `first` to `end` (excluded).
int placedStart(int target, int length, int first, int end)
{
    if (target + length <= end)
        return target;
    if (target - length >= first)
        return target - length;
    const int lastStart = std::max(first, end - length);
    return std::clamp(target - length / 2, first, lastStart);
}

// The application's window, in global coordinates: a popup window holding
// `widget` is too small to bound the menu, its own parent window is not.
QRect windowArea(const QWidget &widget)
{
    const QWidget *window = widget.window();
    while (window->parentWidget())
        window = window->parentWidget()->window();
    return {window->mapToGlobal(QPoint(0, 0)), window->size()};
}

} // namespace

namespace ContextMenus {

QPoint placement(QSize size, QPoint target, QRect area)
{
    return {placedStart(target.x(), size.width(), area.left(), area.left() + area.width()),
            placedStart(target.y(), size.height(), area.top(), area.top() + area.height())};
}

QAction *exec(QMenu &menu, const QWidget &clicked, QPoint globalTarget)
{
    return menu.exec(placement(menu.sizeHint(), globalTarget, windowArea(clicked)));
}

} // namespace ContextMenus
