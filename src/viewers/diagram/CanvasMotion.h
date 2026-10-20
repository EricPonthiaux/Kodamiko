// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtGlobal>

#include <algorithm>
#include <cmath>

// The timing of the canvas's animations, shared by the code that starts
// them (Canvas.cpp) and the code that paints them (CanvasPainting.cpp).
namespace diagram::motion {

inline constexpr qreal geometryMs = 420;
inline constexpr qreal cameraMs = 380;
inline constexpr qreal appearMs = 260;
inline constexpr qreal ghostMs = 220;

// Unlike std::clamp, defined when `low` exceeds `high` (then `low` wins).
inline qreal clamp(qreal value, qreal low, qreal high)
{
    return std::max(low, std::min(value, high));
}

inline qreal easeOut(qreal t)
{
    t = clamp(t, 0, 1);
    return 1 - std::pow(1 - t, 3);
}

} // namespace diagram::motion
