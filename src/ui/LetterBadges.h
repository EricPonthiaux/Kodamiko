// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QIcon>

class QColor;

// Colored badges bearing a letter, readable on light and dark backgrounds
// alike: the kind of an element in a tree (a symbol of the outline, a
// usage...).
namespace LetterBadges {

// The badge of `letter` on `color`; drawn once per letter and color.
QIcon icon(QChar letter, const QColor &color);

} // namespace LetterBadges
