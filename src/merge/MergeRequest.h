// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "MergeDocument.h"
#include "git/Integration.h"

#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>
#include <utility>

// What the agent is asked for the open conflicts of one file, and how its
// answer becomes proposals (MergeDocument::Proposal). The agent reads the
// file from a copy where each open conflict lies between numbered markers,
// as git writes them with the common ancestor (diff3), and may read the
// project; it changes nothing.
namespace MergeRequest {

// The copy the agent reads, and the stretch of each open conflict in it,
// conflict 1 first.
struct Conflicts {
    QByteArray text;
    QList<ThreeWayMerge::Chunk> chunks;
};

// The open conflicts of `document`, the other stretches as the result has
// them.
Conflicts conflicts(const MergeDocument &document, const Integration::ConflictSides &sides);

struct Request {
    QString path;  // the file, a repository path
    QString title; // the operation, "Merging feature into main"
    Integration::ConflictSides sides;
    QString copyPath; // where Conflicts::text was written
    qsizetype conflictCount = 0;
};

QString prompt(const Request &request);
// The JSON Schema of the answer: a proposal per conflict the agent resolves.
QJsonObject schema();

using Proposals = QList<std::pair<ThreeWayMerge::Chunk, MergeDocument::Proposal>>;

// The proposals of `answer` for the conflicts `chunks` (Conflicts::chunks);
// nothing if the answer does not follow the schema. A proposal for a
// conflict that does not exist, or with a resolution not in the schema, is
// left out. The lines written by the agent take the line endings of the
// conflict's sides.
std::optional<Proposals> parse(const QString &answer, const QList<ThreeWayMerge::Chunk> &chunks);

} // namespace MergeRequest
