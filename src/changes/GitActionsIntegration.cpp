// SPDX-License-Identifier: GPL-3.0-only

#include "GitActions.h"
#include "git/GitCommands.h"
#include "git/Integration.h"

#include <memory>

void GitActions::merge(const QString &branch)
{
    runIntegration(tr("Merge"), {.operation = tr("merge"), .sides = {{}, branch}, .commitId = {}},
                   [branch](const RepositoryInfo &repository) {
                       Integration::merge(repository, branch);
                       return QObject::tr("Merged %1 into %2").arg(branch, repository.branch);
                   });
}

void GitActions::rebase(const QString &onto)
{
    runIntegration(tr("Rebase"), {.operation = tr("rebase"), .sides = {onto, {}}, .commitId = {}},
                   [onto](const RepositoryInfo &repository) {
                       Integration::rebase(repository, onto);
                       return QObject::tr("Rebased %1 onto %2").arg(repository.branch, onto);
                   });
}

void GitActions::continueOperation()
{
    runIntegration(tr("Continue"), {}, [](const RepositoryInfo &repository) {
        Integration::continueOperation(repository);
        return QObject::tr("The %1 went on").arg(repository.operation);
    });
}

void GitActions::finishMerge(const QString &message)
{
    runIntegration(tr("Finish merge"), {}, [message](const RepositoryInfo &repository) {
        Integration::finishMerge(repository, message);
        return QObject::tr("Merge completed");
    });
}

void GitActions::runIntegration(const QString &title, Integration::Completion completion, Operation operation)
{
    const auto result = std::make_shared<std::optional<Integration::Completion>>();
    run(
        title,
        [completion = std::move(completion), operation = std::move(operation),
         result](const RepositoryInfo &repository) mutable {
            if (completion.operation.isEmpty()) {
                completion.operation = repository.operation;
                completion.sides = Integration::conflictSides(repository);
            } else if (completion.operation == QObject::tr("merge")) {
                completion.sides.current = repository.branch;
            } else {
                completion.sides.incoming = repository.branch;
            }
            const QString message = operation(repository);
            const std::optional<RepositoryInfo> after = RepositoryInfo::read(repository.workDir);
            if (!after)
                throw GitError(QObject::tr("The repository could not be read after the operation"));
            const bool changed = !repository.operation.isEmpty() || after->headId != repository.headId;
            if (after->operation.isEmpty() && changed) {
                completion.commitId = after->headId;
                *result = completion;
            }
            return message;
        },
        {},
        [this, result] {
            if (*result)
                emit integrationFinished(**result);
        });
}

void GitActions::abortOperation()
{
    const std::optional<RepositoryInfo> current = repository();
    if (!current || current->operation.isEmpty())
        return;
    confirm(tr("Abort"),
            tr("Abort the %1? The branch and the files go back to how they were before it started.")
                .arg(current->operation),
            tr("Abort %1").arg(current->operation), [this] {
                run(
                    tr("Abort"),
                    [](const RepositoryInfo &repository) {
                        Integration::abortOperation(repository);
                        return QObject::tr("The %1 was aborted").arg(repository.operation);
                    },
                    {}, [this] { emit integrationAborted(); });
            });
}

void GitActions::skipCommit()
{
    runIntegration(tr("Skip"), {}, [](const RepositoryInfo &repository) {
        Integration::skipCommit(repository);
        return QObject::tr("Commit skipped");
    });
}
