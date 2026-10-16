#pragma once

#include "syntax/Theme.h"

#include <QPalette>

// The editor theme applied to the whole interface.
namespace ThemeStyle {

// Application palette, from the theme's colors.
QPalette palette(const Theme &theme);
// Style sheet of the controls (fields, buttons, menus, tabs, lists, scroll
// bars), the same for every theme, in its colors.
QString styleSheet(const Theme &theme);
// Sets both on the application.
void apply(const Theme &theme);

} // namespace ThemeStyle
