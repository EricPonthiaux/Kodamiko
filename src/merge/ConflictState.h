// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/Theme.h"

#include <QColor>
#include <QString>

// Where a conflict of the file shown stands, as the navigator, the overview
// and the focus frame of the merge view show it.
enum class ConflictState {
    Open,     // to resolve
    Proposed, // to resolve, with a proposal of the agent
    Resolved,
};

namespace ConflictStates {

// The color of `state`: the conflicts' red, the proposals' purple, a green.
QColor color(ConflictState state, const Theme &theme);
// "to resolve", "proposal", "resolved".
QString name(ConflictState state);

} // namespace ConflictStates
