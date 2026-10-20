// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Theme.h"

#include <QList>

// Five of the most installed VS Code themes (One Dark Pro, Dracula, GitHub
// Dark, Tokyo Night, Catppuccin Mocha), redrawn with the colors of their
// published extensions; MIT, their notices in third_party/vscode-themes.
// Unlike KdeThemes, the interface around the editor takes the theme's own
// colors rather than colors derived from the editor's.
namespace VsCodeThemes {

// The five themes, in that order. Their names carry no "kde-" prefix: they
// are offered with Duet's own.
QList<Theme> all();

} // namespace VsCodeThemes
