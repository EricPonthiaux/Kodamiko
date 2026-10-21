// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "EditTargets.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <variant>

// What a refactoring changed, in order, to revert it: texts changed in
// editors or in files, files created, renamed, or moved to the trash.
class EditJournal {
public:
    // `path`'s text went from `before` to `after`, in its editor (left
    // unsaved) or on disk.
    struct TextChange {
        QString path;
        QString before;
        QString after;
    };

    struct Creation {
        QString path;
    };

    // A file or folder renamed or moved.
    struct Move {
        QString from;
        QString to;
    };

    struct Trashing {
        QString path;
        QString pathInTrash; // where QFile::moveToTrash() put it
    };

    using Entry = std::variant<TextChange, Creation, Move, Trashing>;

    void record(Entry entry) { entries_.append(std::move(entry)); }

    bool isEmpty() const { return entries_.isEmpty(); }

    const QList<Entry> &entries() const { return entries_; }

    // The texts changed since the refactoring, by path as they are now (a
    // renamed file under its new name): reverting would lose those changes.
    QStringList changedSince(const EditTargets &targets) const;
    // Reverts every change, last first: texts back as they were (in the
    // editors as one undo step), created files to the trash, moved ones
    // back, trashed ones out of the trash. Does not check changedSince().
    // Returns what could not be reverted, one sentence each.
    QStringList revert(const EditTargets &targets) const;

private:
    QList<Entry> entries_;
};
