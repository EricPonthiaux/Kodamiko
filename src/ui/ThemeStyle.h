// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Theme.h"

#include <QFont>
#include <QPalette>

class QWidget;

// The editor theme applied to the whole interface.
namespace ThemeStyle {

// Application palette, from the theme's colors.
QPalette palette(const Theme &theme);
// Style sheet of the controls (fields, buttons, menus, tabs, lists, scroll
// bars), the same for every theme, in its colors.
QString styleSheet(const Theme &theme);
// Sets both on the application.
void apply(const Theme &theme);
// The theme apply() set last, as QApplication::palette() is the palette:
// for a popup made where no theme is at hand. Theme::kodamiko() before.
const Theme &applied();
// Sets the application font (Preferences ▸ Fonts) on every widget
// without a font of its own. Under a style sheet, QApplication::setFont
// alone reaches only the top-level windows (the popup menus), not their
// children (the menu bar, the tabs).
void applyFont(const QFont &font);
// Selected menu items and rows: slate blue, the accent little saturated, on
// which the theme's text stays readable.
QColor selectionColor(const Theme &theme);
// `base` with `share` (0 to 1) of `tint` mixed in, opaque: the tints of the
// interface, which follow the theme.
QColor mixed(const QColor &base, const QColor &tint, qreal share);
// Sets `sheet` on `widget` only when it differs from the current one:
// QWidget::setStyleSheet restyles the widget and all its children even for
// the same string, too costly for a sheet updated at each keystroke.
void setStyleSheetIfChanged(QWidget *widget, const QString &sheet);

} // namespace ThemeStyle
