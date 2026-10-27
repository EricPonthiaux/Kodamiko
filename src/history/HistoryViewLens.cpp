// SPDX-License-Identifier: GPL-3.0-only

// HistoryView: a file's lines by age (FileLens), in place of the diff while
// the history is a file's and Lines by Age is on.

#include "FileLens.h"
#include "HistoryView.h"
#include "changes/ChangeMap.h"
#include "changes/DiffFileStrip.h"
#include "changes/DiffHeaderBar.h"
#include "changes/DiffView.h"

#include <QToolButton>
#include <QTreeWidget>

namespace {

constexpr int idRole = Qt::UserRole; // as in HistoryView.cpp

} // namespace

void HistoryView::showLinesByAge(bool shown)
{
    diffHeader_->setVisible(!shown);
    diff_->setVisible(!shown);
    changeMap_->setVisible(!shown);
    // A file's history: one file, no strip to show again.
    fileStrip_->setVisible(!shown && diff_->fileSummaries().size() > 1);
    lens_->setVisible(shown);
    if (shown)
        readBlame();
}

// Blame walks the file's whole history: off the UI thread, after the log.
void HistoryView::readBlame()
{
    const int generation = ++blameGeneration_;
    lens_->showMessage(tr("Reading who wrote each line of %1…").arg(pathFilter_));
    pool_.start([this, folder = folder_, path = pathFilter_, generation] {
        std::optional<FileBlame> blame;
        QString error;
        try {
            blame = FileBlame::read(folder, path);
        } catch (const GitError &exception) {
            error = QString::fromStdString(exception.what());
        }
        QMetaObject::invokeMethod(
            this, [this, path, blame, error, generation] { onBlameRead(path, blame, error, generation); },
            Qt::QueuedConnection);
    });
}

void HistoryView::onBlameRead(const QString &path,
                              const std::optional<FileBlame> &blame,
                              const QString &error,
                              int generation)
{
    // Another file, or read again since.
    if (generation != blameGeneration_)
        return;
    if (!blame) {
        lens_->showMessage(error);
        return;
    }
    lens_->showBlame(path, *blame);
    const QList<QTreeWidgetItem *> selected = commits_->selectedItems();
    lens_->setChosenCommit(selected.isEmpty() ? QString() : selected.first()->data(0, idRole).toString());
}

void HistoryView::chooseCommitOfLens(const QString &id)
{
    for (int i = 0; i < commits_->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = commits_->topLevelItem(i);
        if (item->data(0, idRole).toString() == id) {
            commits_->setCurrentItem(item);
            commits_->scrollToItem(item);
            return;
        }
    }
    // Older than the commits read: lit in the lens only.
    lens_->setChosenCommit(id);
}
