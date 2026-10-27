// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Theme.h"

#include <QList>

// Eight dark themes of Duet's own, "Duet-Slate", "Duet-Fjord"…, drawn only
// with the colors of Material Design's palette (MaterialPalette). Like Duet's
// theme, each gives the interface around the editor its own colors.
namespace MaterialThemes {

// Their names are "duet-" and the theme's: "duet-slate".
QList<Theme> all();

} // namespace MaterialThemes
