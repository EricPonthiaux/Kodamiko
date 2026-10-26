// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "EditJournal.h"

#include <QObject>

#include <vector>

// The refactorings made (renames, quick fixes…), so that the last one can
// be undone in every file it changed, open in an editor or not, and
// whatever was done in the editors since; and redone once undone, until
// another refactoring is made.
class RefactoringHistory : public QObject {
    Q_OBJECT

public:
    explicit RefactoringHistory(QObject *parent = nullptr);

    // `description` names it in a menu: "Rename “total” to “product”".
    // The refactorings undone can no longer be redone.
    void record(const QString &description, EditJournal journal);
    // What undoLast() would undo; empty when there is nothing to undo.
    QString lastDescription() const;
    // Undoes the last refactoring. When a file it changed has changed
    // since, nothing is undone and the refactoring stays the last one.
    // Returns why it was not undone, or what could not be, one sentence
    // each; empty once undone.
    QStringList undoLast(const EditTargets &targets);

    // What redoLast() would redo; empty when there is nothing to redo.
    QString nextRedoDescription() const;
    // Redoes the refactoring undone last, as undoLast() undoes one.
    QStringList redoLast(const EditTargets &targets);

    // Whether `text` in `path` is what the last refactoring left there:
    // the editor's undo would undo part of it.
    bool isLeftByLast(const QString &path, const QString &text) const;
    // Whether `text` in `path` is what the refactoring to redo found there.
    bool isFoundByNextRedo(const QString &path, const QString &text) const;

signals:
    // lastDescription() may have changed.
    void changed();

private:
    struct Refactoring {
        QString description;
        EditJournal journal;
    };

    std::vector<Refactoring> refactorings_;
    std::vector<Refactoring> undone_; // the last undone at the back
};
