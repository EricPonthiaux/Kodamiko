#pragma once

#include "FileChange.h"

#include <QColor>

// Lettre et couleur d'un type de changement, comme dans « git status ».
inline QString changeKindLetter(ChangeKind kind)
{
    switch (kind) {
    case ChangeKind::Added: return QStringLiteral("A");
    case ChangeKind::Modified: return QStringLiteral("M");
    case ChangeKind::Deleted: return QStringLiteral("D");
    case ChangeKind::Renamed: return QStringLiteral("R");
    }
    return {};
}

inline QColor changeKindColor(ChangeKind kind)
{
    switch (kind) {
    case ChangeKind::Added: return QColor("#3fa34d");
    case ChangeKind::Modified: return QColor("#d19a1f");
    case ChangeKind::Deleted: return QColor("#d14b4b");
    case ChangeKind::Renamed: return QColor("#4b8bd1");
    }
    return {};
}

inline QString changeLabel(const FileChange &change)
{
    return change.kind == ChangeKind::Renamed ? QStringLiteral("%1 → %2").arg(change.oldPath, change.path)
                                              : change.path;
}
