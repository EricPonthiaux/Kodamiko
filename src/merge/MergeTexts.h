// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/ConflictVersions.h"
#include "git/Integration.h"

#include <QString>

class MergeDocument;

// What the merge view says of the operation and of each conflicted file.
namespace MergeTexts {

// "Merging feature into main", for the operation in progress
// (RepositoryInfo::operation), empty for none (a stash applied).
QString title(const QString &operation, const Integration::ConflictSides &sides);

// Where `file` is: what each side did to it, or, with its resolution
// `document` (null when not opened), the conflicts left.
QString fileState(const ConflictVersions::File &file,
                  const Integration::ConflictSides &sides,
                  const MergeDocument *document);

} // namespace MergeTexts
