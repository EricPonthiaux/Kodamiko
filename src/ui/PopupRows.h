// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QStyle>

class QPainter;
class QRectF;
struct Theme;

// The rows of the popups' lists drawn by a delegate of their own
// (completion, branches, snapshots, run configurations), lit as the rows
// popupStyleSheet() styles: the selection in ThemeStyle::selectionColor,
// the row under the mouse in the theme's hover, with the same corners.
namespace PopupRows {

// Fills `row` for `state` (QStyle::State_Selected, State_MouseOver); leaves
// it empty otherwise. The painter's pen and brush are changed.
void paintBackground(QPainter *painter, const QRectF &row, QStyle::State state, const Theme &theme);

} // namespace PopupRows
