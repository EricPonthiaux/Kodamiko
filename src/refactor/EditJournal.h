// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "EditTargets.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>
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

    // The texts changed since revert(), by path as they were before the
    // refactoring: replaying would lose those changes.
    QStringList changedSinceRevert(const EditTargets &targets) const;
    // Makes every change again after revert(), first first, and keeps where
    // the trashed files go this time. Does not check changedSinceRevert().
    // Returns what could not be made, one sentence each.
    QStringList replay(const EditTargets &targets);

    // The text the changes left in `path` (as it is named now), if they
    // changed it.
    std::optional<QString> textAfter(const QString &path) const;
    // The text `path` had (as it was named before the changes), if they
    // changed it.
    std::optional<QString> textBefore(const QString &path) const;

private:
    QList<Entry> entries_;
};
