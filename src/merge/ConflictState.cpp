// SPDX-License-Identifier: GPL-3.0-only

#include "ConflictState.h"

#include <QObject>

namespace ConflictStates {

QColor color(ConflictState state, const Theme &theme)
{
    // The shades of MergePane for the open conflicts and the proposals.
    switch (state) {
    case ConflictState::Open:
        return theme.isDark ? QColor("#ff6b6b") : QColor("#cf222e");
    case ConflictState::Proposed:
        return theme.isDark ? QColor("#d59cff") : QColor("#8250df");
    case ConflictState::Resolved:
        return theme.isDark ? QColor("#8fd18a") : QColor("#246f3b");
    }
    return theme.foreground;
}

QString name(ConflictState state)
{
    switch (state) {
    case ConflictState::Open:
        return QObject::tr("to resolve");
    case ConflictState::Proposed:
        return QObject::tr("proposal");
    case ConflictState::Resolved:
        return QObject::tr("resolved");
    }
    return {};
}

} // namespace ConflictStates
