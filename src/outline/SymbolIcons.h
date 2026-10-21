// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "OutlineSymbol.h"

#include <QIcon>
#include <QString>

// How a kind of symbol shows in the trees (the outline, the hierarchy):
// a badge with its letter and its name.
namespace SymbolIcons {

// "C" for a class, "m" for a method..., each kind in its color.
QIcon icon(SymbolKind kind);
// "Class", "Method"..., translated.
QString name(SymbolKind kind);

} // namespace SymbolIcons
