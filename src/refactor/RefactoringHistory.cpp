// SPDX-License-Identifier: GPL-3.0-only

#include "RefactoringHistory.h"

#include <QFileInfo>

namespace {

// Older refactorings are forgotten: nobody undoes that far back.
constexpr std::size_t maximumRefactorings = 50;

} // namespace

RefactoringHistory::RefactoringHistory(QObject *parent)
    : QObject(parent)
{
}

void RefactoringHistory::record(const QString &description, EditJournal journal)
{
    if (journal.isEmpty())
        return;
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
    if (!modified.isEmpty()) {
        QStringList names;
        for (const QString &path : modified)
            names.append(QFileInfo(path).fileName());
        return {tr("Cannot undo “%1”: %2 changed since", nullptr, int(names.size()))
                    .arg(refactorings_.back().description, names.join(QStringLiteral(", ")))};
    }
    const Refactoring refactoring = std::move(refactorings_.back());
    refactorings_.pop_back();
    emit changed();
    return refactoring.journal.revert(targets);
}
