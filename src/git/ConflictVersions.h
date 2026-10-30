// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "RepositoryStatus.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

// The versions git keeps in the index of the files it could not merge: the
// common ancestor, the current side and the incoming side (see
// Integration::ConflictSide), whatever the file on disk now holds.
namespace ConflictVersions {

// One version of a conflicted file.
struct Version {
    QString path; // repository path
    QByteArray content;

    bool operator==(const Version &) const = default;
};

// Which versions the index has, which tells how the conflict is resolved.
enum class Kind {
    BothModified,       // the three versions
    BothAdded,          // no common ancestor
    DeletedByCurrent,   // the ancestor and the incoming side
    DeletedByIncoming,  // the ancestor and the current side
    RenamedOnBothSides, // the ancestor's file, under another name on each side
    // What is left of a rename that could not be paired with its other
    // parts (the file was also changed): one version only.
    DeletedByBoth,   // the ancestor only
    AddedByCurrent,  // the current side only
    AddedByIncoming, // the incoming side only
};

struct File {
    Kind kind = Kind::BothModified;
    // Repository path: the ancestor's for RenamedOnBothSides, where each
    // side's version has its own.
    QString path;
    std::optional<Version> base;
    std::optional<Version> current;
    std::optional<Version> incoming;
    // As git tells it: a null byte among the first 8000 of a version.
    bool isBinary = false;

    bool operator==(const File &) const = default;
};

// The conflicted files of `repository`, by path. A rename on both sides is
// recognized when neither side changed the file's content. Throws GitError.
QList<File> read(const RepositoryInfo &repository);

} // namespace ConflictVersions
