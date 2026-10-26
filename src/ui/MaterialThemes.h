// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Theme.h"

#include <QList>

// Twenty themes of Duet's own, "Duet-Slate", "Duet-Paper"…, drawn only with
// the colors of Material Design's palette (MaterialPalette): ten dark, ten
// light. Like Duet's theme, each gives the interface around the editor its
// own colors.
namespace MaterialThemes {

// The dark themes, then the light ones. Their names are "duet-" and the
// theme's: "duet-slate".
QList<Theme> all();

} // namespace MaterialThemes
