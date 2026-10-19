#pragma once

#include "Preferences.h"

class QFont;
class QWidget;

// The fonts of Preferences ▸ Fonts given to the running interface.
// The code font goes with the theme instead (Preferences::applyTypography).
namespace FontSettings {

// The interface font to the application, the tree font to the trees of
// `window`; each only when it differs from `previous`.
void apply(const Preferences &preferences, const Preferences &previous, const QWidget *window);
// `font` to every tree of `window` (files, outline, changes), with the
// proportions of TreeMetrics.
void applyTreeFont(const QFont &font, const QWidget *window);

} // namespace FontSettings
