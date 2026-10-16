#include "SnapshotsDialog.h"

#include "git/GitCommands.h"
#include "workspace/GripSplitter.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

SnapshotsDialog::SnapshotsDialog(const QString &folder, GitCommands *commands, QWidget *parent)
    : QDialog(parent)
    , folder_(folder)
    , commands_(commands)
    , list_(new QListWidget(this))
    , details_(new QPlainTextEdit(this))
{
    setWindowTitle(tr("Restore a Snapshot"));
    resize(760, 460);

    auto *help = new QLabel(tr("Before discarding changes, Duet keeps a copy of the affected files; before each "
                               "Claude prompt, a copy of the whole working directory. Restoring puts these files "
                               "back on disk as they were; their current state is first copied into a new "
                               "snapshot."),
                            this);
    help->setWordWrap(true);

    details_->setReadOnly(true);
    auto *splitter = new GripSplitter(Qt::Horizontal, this);
    splitter->addWidget(list_);
    splitter->addWidget(details_);
    splitter->setSizes({380, 380});

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    restore_ = buttons->addButton(tr("Restore These Files"), QDialogButtonBox::ActionRole);
    connect(restore_, &QPushButton::clicked, this, &SnapshotsDialog::restoreSelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(help);
    layout->addWidget(splitter, 1);
    layout->addWidget(buttons);

    QString error;
    try {
        snapshots_ = Snapshots::list(folder_);
    } catch (const GitError &exception) {
        error = QString::fromStdString(exception.what());
    }
    for (const Snapshot &snapshot : std::as_const(snapshots_)) {
        new QListWidgetItem(QStringLiteral("%1 — %2").arg(QLocale().toString(snapshot.date, QLocale::ShortFormat),
                                                           snapshot.reason),
                            list_);
    }
    connect(list_, &QListWidget::currentRowChanged, this, &SnapshotsDialog::showDetails);
    if (!snapshots_.isEmpty())
        list_->setCurrentRow(0);
    else
        details_->setPlainText(error.isEmpty() ? tr("No snapshots for this repository.") : error);
    restore_->setEnabled(!snapshots_.isEmpty());
}

void SnapshotsDialog::showDetails()
{
    const int row = list_->currentRow();
    if (row < 0)
        return;
    const Snapshot &snapshot = snapshots_.at(row);
    const QString files = snapshot.paths.isEmpty() ? tr("(none)") : snapshot.paths.join(QLatin1Char('\n'));
    if (snapshot.wholeWorkingTree)
        details_->setPlainText(tr("%1\n\nWhole working directory: restoring undoes every change made since, "
                                  "ignored files excepted.\n\nFiles changed since the last commit:\n%2\n\nCommit: %3")
                                   .arg(snapshot.reason, files, snapshot.commitId));
    else
        details_->setPlainText(tr("%1\n\nFiles:\n%2\n\nCommit: %3").arg(snapshot.reason, files, snapshot.commitId));
}

void SnapshotsDialog::restoreSelected()
{
    const int row = list_->currentRow();
    if (row < 0)
        return;
    std::optional<RepositoryInfo> repository;
    try {
        repository = RepositoryInfo::read(folder_);
    } catch (const GitError &) {
    }
    if (!repository)
        return;
    commands_->run(tr("Restoring a snapshot"), [repository = *repository, snapshot = snapshots_.at(row)] {
        const int count = Snapshots::restore(repository, snapshot);
        return QObject::tr("%n file(s) restored", nullptr, count);
    });
    accept();
}
