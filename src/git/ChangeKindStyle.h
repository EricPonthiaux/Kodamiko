#pragma once

#include "FileChange.h"

#include <QColor>
#include <QObject>

// Letter and color of a change kind, as in "git status".
inline QString changeKindLetter(ChangeKind kind)
{
    switch (kind) {
    case ChangeKind::Added:
        return QStringLiteral("A");
    case ChangeKind::Modified:
        return QStringLiteral("M");
    case ChangeKind::Deleted:
        return QStringLiteral("D");
    case ChangeKind::Renamed:
        return QStringLiteral("R");
    case ChangeKind::Conflicted:
        return QStringLiteral("U");
    }
    return {};
}

inline QColor changeKindColor(ChangeKind kind)
{
    switch (kind) {
    case ChangeKind::Added:
        return QColor("#3fa34d");
    case ChangeKind::Modified:
        return QColor("#d19a1f");
    case ChangeKind::Deleted:
        return QColor("#d14b4b");
    case ChangeKind::Renamed:
        return QColor("#4b8bd1");
    case ChangeKind::Conflicted:
        return QColor("#b65fd1");
    }
    return {};
}

// "modified", "renamed from old.cpp"…, as a diff's title bar says it.
inline QString changeStatusText(const FileChange &change)
{
    switch (change.kind) {
    case ChangeKind::Added:
        return QObject::tr("added");
    case ChangeKind::Modified:
        return QObject::tr("modified");
    case ChangeKind::Deleted:
        return QObject::tr("deleted");
    case ChangeKind::Renamed:
        return QObject::tr("renamed from %1").arg(change.oldPath);
    case ChangeKind::Conflicted:
        return QObject::tr("in conflict");
    }
    return {};
}

inline QString changeLabel(const FileChange &change)
{
    return change.kind == ChangeKind::Renamed ? QStringLiteral("%1 → %2").arg(change.oldPath, change.path)
                                              : change.path;
}
