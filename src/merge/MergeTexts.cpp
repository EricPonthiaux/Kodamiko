// SPDX-License-Identifier: GPL-3.0-only

#include "MergeTexts.h"

#include "MergeDocument.h"

#include <QObject>

#include <algorithm>

namespace {

using ConflictVersions::Kind;

QString pathOf(const std::optional<ConflictVersions::Version> &version)
{
    return version ? version->path : QString();
}

QString kindState(const ConflictVersions::File &file, const Integration::ConflictSides &sides)
{
    switch (file.kind) {
    case Kind::BothModified:
        return QObject::tr("Changed on both sides");
    case Kind::BothAdded:
        return QObject::tr("Added on both sides");
    case Kind::DeletedByCurrent:
        return QObject::tr("Deleted on %1, changed on %2").arg(sides.current, sides.incoming);
    case Kind::DeletedByIncoming:
        return QObject::tr("Deleted on %1, changed on %2").arg(sides.incoming, sides.current);
    case Kind::RenamedOnBothSides:
        return QObject::tr("Renamed %1 on %2 and %3 on %4")
            .arg(pathOf(file.current), sides.current, pathOf(file.incoming), sides.incoming);
    case Kind::DeletedByBoth:
        return QObject::tr("Deleted on both sides");
    case Kind::AddedByCurrent:
        return QObject::tr("Added on %1 only").arg(sides.current);
    case Kind::AddedByIncoming:
        return QObject::tr("Added on %1 only").arg(sides.incoming);
    }
    return {};
}

} // namespace

namespace MergeTexts {

QString title(const QString &operation, const Integration::ConflictSides &sides)
{
    if (operation == QObject::tr("merge"))
        return QObject::tr("Merging %1 into %2").arg(sides.incoming, sides.current);
    if (operation == QObject::tr("rebase"))
        return QObject::tr("Rebasing onto %1: %2").arg(sides.current, sides.incoming);
    if (operation == QObject::tr("cherry-pick"))
        return QObject::tr("Cherry-picking %1 onto %2").arg(sides.incoming, sides.current);
    if (operation == QObject::tr("revert"))
        return QObject::tr("%1 on %2").arg(sides.incoming, sides.current);
    return QObject::tr("Conflicts on %1").arg(sides.current);
}

QString fileState(const ConflictVersions::File &file,
                  const Integration::ConflictSides &sides,
                  const MergeDocument *document)
{
    if (document) {
        const int left = document->unresolvedCount();
        if (left == 0)
            return QObject::tr("Resolved, not saved yet");
        const auto total = std::ranges::count_if(document->segments(), [](const MergeDocument::Segment &segment) {
            return segment.chunk.kind == ThreeWayMerge::ChunkKind::Conflict;
        });
        return QObject::tr("%1 of %n conflict(s) resolved", nullptr, int(total)).arg(total - left);
    }
    if (file.isBinary)
        return QObject::tr("Binary file changed on both sides");
    return kindState(file, sides);
}

} // namespace MergeTexts
