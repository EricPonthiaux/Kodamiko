// SPDX-License-Identifier: GPL-3.0-only

#include "RefactoringHistory.h"

#include <QFileInfo>

namespace {

// Older refactorings are forgotten: nobody undoes that far back.
constexpr std::size_t maximumRefactorings = 50;

QString fileNames(const QStringList &paths)
{
    QStringList names;
    for (const QString &path : paths)
        names.append(QFileInfo(path).fileName());
    return names.join(QStringLiteral(", "));
}

} // namespace

RefactoringHistory::RefactoringHistory(QObject *parent)
    : QObject(parent)
{
}

void RefactoringHistory::record(const QString &description, EditJournal journal)
{
    if (journal.isEmpty())
        return;
    undone_.clear();
    refactorings_.push_back({description, std::move(journal)});
    if (refactorings_.size() > maximumRefactorings)
        refactorings_.erase(refactorings_.begin());
    emit changed();
}

QString RefactoringHistory::lastDescription() const
{
    return refactorings_.empty() ? QString() : refactorings_.back().description;
}

QStringList RefactoringHistory::undoLast(const EditTargets &targets)
{
    if (refactorings_.empty())
        return {};
    const QStringList modified = refactorings_.back().journal.changedSince(targets);
    if (!modified.isEmpty())
        return {tr("Cannot undo “%1”: %2 changed since", nullptr, int(modified.size()))
                    .arg(refactorings_.back().description, fileNames(modified))};
    Refactoring refactoring = std::move(refactorings_.back());
    refactorings_.pop_back();
    const QStringList errors = refactoring.journal.revert(targets);
    undone_.push_back(std::move(refactoring));
    emit changed();
    return errors;
}

QString RefactoringHistory::nextRedoDescription() const
{
    return undone_.empty() ? QString() : undone_.back().description;
}

QStringList RefactoringHistory::redoLast(const EditTargets &targets)
{
    if (undone_.empty())
        return {};
    const QStringList modified = undone_.back().journal.changedSinceRevert(targets);
    if (!modified.isEmpty())
        return {tr("Cannot redo “%1”: %2 changed since", nullptr, int(modified.size()))
                    .arg(undone_.back().description, fileNames(modified))};
    Refactoring refactoring = std::move(undone_.back());
    undone_.pop_back();
    const QStringList errors = refactoring.journal.replay(targets);
    refactorings_.push_back(std::move(refactoring));
    emit changed();
    return errors;
}

bool RefactoringHistory::isLeftByLast(const QString &path, const QString &text) const
{
    return !refactorings_.empty() && refactorings_.back().journal.textAfter(path) == text;
}

bool RefactoringHistory::isFoundByNextRedo(const QString &path, const QString &text) const
{
    return !undone_.empty() && undone_.back().journal.textBefore(path) == text;
}
