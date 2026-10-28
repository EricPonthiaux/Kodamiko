// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Theme.h"

#include <QList>

// Eight dark themes of Kodamiko's own, "Kodamiko-Slate", "Kodamiko-Fjord"…, drawn only
// with the colors of Material Design's palette (MaterialPalette). Like Kodamiko's
// theme, each gives the interface around the editor its own colors.
namespace MaterialThemes {

// Their names are "kodamiko-" and the theme's: "kodamiko-slate".
QList<Theme> all();

} // namespace MaterialThemes
