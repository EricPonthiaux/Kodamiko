#include "GitActions.h"

#include "git/Branches.h"
#include "git/GitCommands.h"
#include "git/Integration.h"
#include "git/Remotes.h"
#include "git/Stashes.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// The repository of `folder`, read again in the job: it may have changed
// since the user asked.
RepositoryInfo repositoryOf(const QString &folder)
{
    const std::optional<RepositoryInfo> repository = RepositoryInfo::read(folder);
    if (!repository)
        throw GitError(QObject::tr("The folder is not in a git repository"));
    return *repository;
}

} // namespace

GitActions::GitActions(GitCommands *commands, QWidget *window, std::function<QString()> folder, QObject *parent)
    : QObject(parent)
    , commands_(commands)
    , window_(window)
    , folder_(std::move(folder))
{
}

std::optional<RepositoryInfo> GitActions::repository()
{
    std::optional<RepositoryInfo> repository;
    try {
        repository = RepositoryInfo::read(folder_());
    } catch (const GitError &error) {
        emit statusMessage(error.message());
        return std::nullopt;
    }
    if (!repository)
        emit statusMessage(tr("The folder is not in a git repository"));
    return repository;
}

void GitActions::run(const QString &title, Operation operation, std::function<bool(const GitError &)> onFailure)
{
    commands_->run(
        title, [folder = folder_(), operation = std::move(operation)] { return operation(repositoryOf(folder)); },
        [this] { emit changed(); },
        [this, onFailure = std::move(onFailure)](const GitError &error) {
            // A failure can leave the repository changed too (conflicts).
            emit changed();
            return onFailure && onFailure(error);
        });
}

void GitActions::runRemote(const QString &title,
                           std::function<QString(const RepositoryInfo &, const Git::Feedback &)> operation,
                           std::function<bool(const GitError &)> onFailure)
{
    commands_->runLong(
        title,
        [folder = folder_(), operation = std::move(operation)](const Git::Feedback &feedback) {
            return operation(repositoryOf(folder), feedback);
        },
        [this] { emit changed(); },
        [this, title, onFailure = std::move(onFailure)](const GitError &error) {
            emit changed();
            if (error.kind() == GitError::Kind::Cancelled) {
                emit statusMessage(tr("%1: cancelled").arg(title));
                return true;
            }
            return onFailure && onFailure(error);
        });
}

bool GitActions::confirm(const QString &title, const QString &question, const QString &action)
{
    QMessageBox box(QMessageBox::Question, title, question, QMessageBox::Cancel, window_);
    const QPushButton *accept = box.addButton(action, QMessageBox::AcceptRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == accept;
}

void GitActions::fetch()
{
    runRemote(tr("Fetch"), &Remotes::fetch);
}

void GitActions::pull()
{
    runRemote(tr("Pull"), &Remotes::pull, [this](const GitError &error) { return onPullFailed(error); });
}

void GitActions::push()
{
    runRemote(
        tr("Push"),
        [](const RepositoryInfo &repository, const Git::Feedback &feedback) {
            return Remotes::push(repository, feedback);
        },
        [this](const GitError &error) { return onPushFailed(error); });
}

void GitActions::sync()
{
    runRemote(tr("Sync"), &Remotes::sync,
              [this](const GitError &error) { return onPullFailed(error) || onPushFailed(error); });
}

bool GitActions::onPullFailed(const GitError &error)
{
    if (error.kind() != GitError::Kind::Diverged)
        return false;
    const std::optional<RepositoryInfo> current = repository();
    if (!current || current->upstream.isEmpty())
        return false;
    QMessageBox box(QMessageBox::Question, tr("Pull"), error.message(), QMessageBox::Cancel, window_);
    box.setInformativeText(tr("Rebase replays your own commits on top of %1; merge joins both with a merge commit.")
                               .arg(current->upstream));
    QPushButton *rebaseButton = box.addButton(tr("Rebase"), QMessageBox::AcceptRole);
    const QPushButton *mergeButton = box.addButton(tr("Merge"), QMessageBox::AcceptRole);
    box.setDefaultButton(rebaseButton);
    box.exec();
    if (box.clickedButton() == rebaseButton)
        rebase(current->upstream);
    else if (box.clickedButton() == mergeButton)
        merge(current->upstream);
    return true;
}

bool GitActions::onPushFailed(const GitError &error)
{
    if (error.kind() != GitError::Kind::Rejected)
        return false;
    QMessageBox box(QMessageBox::Warning, tr("Push"), error.message(), QMessageBox::Cancel, window_);
    box.setInformativeText(tr("Pull brings their commits in first. Overwriting deletes them from the remote, "
                              "unless someone pushed others since the last fetch."));
    QPushButton *pullButton = box.addButton(tr("Pull"), QMessageBox::AcceptRole);
    const QPushButton *forceButton = box.addButton(tr("Overwrite"), QMessageBox::DestructiveRole);
    box.setDefaultButton(pullButton);
    box.exec();
    if (box.clickedButton() == pullButton) {
        pull();
    } else if (box.clickedButton() == forceButton) {
        runRemote(tr("Push"), [](const RepositoryInfo &repository, const Git::Feedback &feedback) {
            return Remotes::push(repository, feedback, true);
        });
    }
    return true;
}

void GitActions::switchBranch(const QString &name, bool remote)
{
    run(
        tr("Switch branch"),
        [name, remote](const RepositoryInfo &repository) {
            Branches::switchTo(repository, name, remote);
            return QObject::tr("Switched to %1").arg(remote ? Branches::nameOnRemote(name) : name);
        },
        [this, name, remote](const GitError &error) { return onSwitchFailed(error, name, remote); });
}

bool GitActions::onSwitchFailed(const GitError &error, const QString &name, bool remote)
{
    if (error.kind() != GitError::Kind::LocalChanges)
        return false;
    QMessageBox box(QMessageBox::Question, tr("Switch branch"), tr("Your changes conflict with %1.").arg(name),
                    QMessageBox::Cancel, window_);
    box.setInformativeText(tr("They can be put aside in a stash, then brought back on %1: the conflicts are "
                              "then left for you to resolve.")
                               .arg(name));
    box.setDetailedText(error.message());
    QPushButton *stashButton = box.addButton(tr("Stash and Switch"), QMessageBox::AcceptRole);
    box.setDefaultButton(stashButton);
    box.exec();
    if (box.clickedButton() == stashButton) {
        run(tr("Switch branch"), [name, remote](const RepositoryInfo &repository) {
            Branches::stashAndSwitch(repository, name, remote);
            return QObject::tr("Switched to %1 with your changes").arg(remote ? Branches::nameOnRemote(name) : name);
        });
    }
    return true;
}

void GitActions::checkoutCommit(const QString &id)
{
    if (!confirm(
            tr("Check Out Commit"),
            tr("Check out %1 without a branch? Commits made there belong to no branch until you create one.").arg(id),
            tr("Check Out")))
        return;
    run(
        tr("Check out"),
        [id](const RepositoryInfo &repository) {
            Branches::detach(repository, id);
            return QObject::tr("Checked out %1 (detached HEAD)").arg(id);
        },
        [this, id](const GitError &error) {
            if (error.kind() != GitError::Kind::LocalChanges)
                return false;
            QMessageBox::warning(
                window_, tr("Check out"),
                tr("Your changes conflict with %1: commit or stash them first.\n\n%2").arg(id, error.message()));
            return true;
        });
}

void GitActions::renameBranch(const QString &name)
{
    bool accepted = false;
    QString newName = QInputDialog::getText(window_, tr("Rename Branch"), tr("New name of %1:").arg(name),
                                            QLineEdit::Normal, name, &accepted);
    if (!accepted)
        return;
    newName = Branches::sanitizedName(newName.trimmed());
    if (newName == name)
        return;
    if (const QString problem = Branches::nameError(newName); !problem.isEmpty()) {
        QMessageBox::warning(window_, tr("Rename Branch"), problem);
        return;
    }
    run(tr("Rename branch"), [name, newName](const RepositoryInfo &repository) {
        Branches::rename(repository, name, newName);
        return QObject::tr("Branch %1 renamed %2").arg(name, newName);
    });
}

void GitActions::deleteBranch(const QString &name, bool remote)
{
    if (remote) {
        if (!confirm(tr("Delete Remote Branch"),
                     tr("Delete %1 from %2? Everyone working with this remote loses the branch.")
                         .arg(Branches::nameOnRemote(name), Branches::remoteOf(name)),
                     tr("Delete")))
            return;
        runRemote(tr("Delete remote branch"), [name](const RepositoryInfo &repository, const Git::Feedback &feedback) {
            Branches::removeRemote(repository, name, feedback);
            return QObject::tr("Branch %1 deleted").arg(name);
        });
        return;
    }
    run(
        tr("Delete branch"),
        [name](const RepositoryInfo &repository) {
            Branches::remove(repository, name, false);
            return QObject::tr("Branch %1 deleted").arg(name);
        },
        [this, name](const GitError &error) {
            if (error.kind() != GitError::Kind::Unmerged)
                return false;
            if (confirm(tr("Delete Branch"),
                        tr("%1. Delete it anyway? Its commits will only be reachable from the reflog.")
                            .arg(error.message()),
                        tr("Delete"))) {
                run(tr("Delete branch"), [name](const RepositoryInfo &repository) {
                    Branches::remove(repository, name, true);
                    return QObject::tr("Branch %1 deleted").arg(name);
                });
            }
            return true;
        });
}

void GitActions::trackBranch(const QString &remoteBranch)
{
    run(tr("Track branch"), [remoteBranch](const RepositoryInfo &repository) {
        if (repository.branch.isEmpty() || repository.detached)
            throw GitError(QObject::tr("HEAD is detached: switch to a branch first"));
        Branches::setUpstream(repository, repository.branch, remoteBranch);
        return QObject::tr("%1 now tracks %2").arg(repository.branch, remoteBranch);
    });
}

void GitActions::merge(const QString &branch)
{
    run(tr("Merge"), [branch](const RepositoryInfo &repository) {
        Integration::merge(repository, branch);
        return QObject::tr("Merged %1 into %2").arg(branch, repository.branch);
    });
}

void GitActions::rebase(const QString &onto)
{
    run(tr("Rebase"), [onto](const RepositoryInfo &repository) {
        Integration::rebase(repository, onto);
        return QObject::tr("Rebased %1 onto %2").arg(repository.branch, onto);
    });
}

void GitActions::continueOperation()
{
    run(tr("Continue"), [](const RepositoryInfo &repository) {
        Integration::continueOperation(repository);
        return QObject::tr("The %1 went on").arg(repository.operation);
    });
}

void GitActions::abortOperation()
{
    const std::optional<RepositoryInfo> current = repository();
    if (!current || current->operation.isEmpty())
        return;
    if (!confirm(tr("Abort"),
                 tr("Abort the %1? The branch and the files go back to how they were before it started.")
                     .arg(current->operation),
                 tr("Abort %1").arg(current->operation)))
        return;
    run(tr("Abort"), [](const RepositoryInfo &repository) {
        Integration::abortOperation(repository);
        return QObject::tr("The %1 was aborted").arg(repository.operation);
    });
}

void GitActions::skipCommit()
{
    run(tr("Skip"), [](const RepositoryInfo &repository) {
        Integration::skipCommit(repository);
        return QObject::tr("Commit skipped");
    });
}

void GitActions::stashChanges()
{
    QDialog dialog(window_);
    dialog.setWindowTitle(tr("Stash Changes"));
    auto *explanation = new QLabel(tr("Put your changes aside: the files go back to the last commit, and the "
                                      "changes can be brought back later, on this branch or another."),
                                   &dialog);
    explanation->setWordWrap(true);
    auto *message = new QLineEdit(&dialog);
    message->setPlaceholderText(tr("Message (optional)"));
    auto *untracked = new QCheckBox(tr("Include new files"), &dialog);
    untracked->setChecked(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    buttons->addButton(tr("Stash"), QDialogButtonBox::AcceptRole)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(explanation);
    layout->addWidget(message);
    layout->addWidget(untracked);
    layout->addWidget(buttons);
    dialog.resize(440, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted)
        return;
    run(tr("Stash"), [text = message->text().trimmed(),
                      includeUntracked = untracked->isChecked()](const RepositoryInfo &repository) {
        Stashes::push(repository, text, includeUntracked);
        return QObject::tr("Changes stashed");
    });
}

void GitActions::applyStash(int index, bool drop)
{
    run(
        drop ? tr("Pop stash") : tr("Apply stash"),
        [index, drop](const RepositoryInfo &repository) {
            if (drop)
                Stashes::pop(repository, index);
            else
                Stashes::apply(repository, index);
            return drop ? QObject::tr("Stash brought back and dropped") : QObject::tr("Stash brought back");
        },
        [this](const GitError &error) {
            if (error.kind() != GitError::Kind::LocalChanges)
                return false;
            QMessageBox::warning(
                window_, tr("Stash"),
                tr("Your changes conflict with the stash: commit or stash them first.\n\n%1").arg(error.message()));
            return true;
        });
}

void GitActions::dropStash(int index, const QString &message)
{
    if (!confirm(tr("Drop Stash"), tr("Drop \"%1\"? Its changes are lost.").arg(message), tr("Drop")))
        return;
    run(tr("Drop stash"), [index](const RepositoryInfo &repository) {
        Stashes::drop(repository, index);
        return QObject::tr("Stash dropped");
    });
}
