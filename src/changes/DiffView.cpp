#include "DiffView.h"

#include "ChangeKindStyle.h"
#include "WorkingTreeDiff.h"
#include "editor/EditorWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int pathRole = Qt::UserRole;
constexpr int lineRole = Qt::UserRole + 1; // première ligne du fichier dans le diff
constexpr int deletedRole = Qt::UserRole + 2;

} // namespace

DiffView::DiffView(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , summary_(new QLabel(this))
    , files_(new QListWidget(this))
    , diff_(new EditorWidget(theme, this))
    , splitter_(new QSplitter(Qt::Horizontal, this))
{
    auto *refresh = new QToolButton(this);
    refresh->setText(tr("Rafraîchir"));
    refresh->setToolTip(tr("Recalculer les changements (F5)"));
    refresh->setShortcut(QKeySequence(Qt::Key_F5));
    refresh->setAutoRaise(true);
    connect(refresh, &QToolButton::clicked, this, &DiffView::refresh);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(8, 4, 4, 4);
    header->addWidget(summary_);
    header->addStretch();
    header->addWidget(refresh);

    splitter_->addWidget(files_);
    splitter_->addWidget(diff_);
    splitter_->setStretchFactor(1, 1);
    splitter_->setSizes({300, 1100});

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(splitter_);

    files_->setToolTip(tr("Double-clic : ouvrir le fichier dans l'éditeur"));
    connect(files_, &QListWidget::currentItemChanged, this, &DiffView::scrollToSelectedFile);
    connect(files_, &QListWidget::itemClicked, this, &DiffView::scrollToSelectedFile);
    connect(files_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (!item->data(deletedRole).toBool())
            emit openFileRequested(item->data(pathRole).toString());
    });
}

void DiffView::setFolder(const QString &folder)
{
    folder_ = folder;
    files_->clear();
    diff_->showReadOnlyText({}, QStringLiteral("diff"));
    summary_->clear();
}

void DiffView::refresh()
{
    if (folder_.isEmpty())
        return;

    QList<WorkingTreeChange> changes;
    QString message;
    try {
        if (!WorkingTreeDiff::compute(folder_, &changes))
            message = tr("Ce dossier n'est pas dans un dépôt git : aucun commit auquel comparer les fichiers.");
    } catch (const GitError &error) {
        message = QString::fromStdString(error.what());
    }

    const QString selectedPath = files_->currentItem() ? files_->currentItem()->data(pathRole).toString() : QString();
    const int firstLine = diff_->firstVisibleLine();
    const QSignalBlocker blocker(files_);
    files_->clear();

    QString text;
    int line = 0;
    for (const WorkingTreeChange &change : changes) {
        auto *item = new QListWidgetItem(changeKindLetter(change.change.kind) + QStringLiteral("  ")
                                             + changeLabel(change.change),
                                         files_);
        item->setForeground(changeKindColor(change.change.kind));
        item->setData(pathRole, change.change.path);
        item->setData(lineRole, line);
        item->setData(deletedRole, change.change.kind == ChangeKind::Deleted);
        if (change.change.path == selectedPath)
            files_->setCurrentItem(item);
        text += change.patch;
        line += static_cast<int>(change.patch.count(QLatin1Char('\n')));
    }

    if (!message.isEmpty()) {
        summary_->setText(message);
        diff_->showReadOnlyText({}, QStringLiteral("diff"));
        return;
    }
    summary_->setText(changes.isEmpty()
                          ? tr("Aucun changement depuis le dernier commit")
                          : tr("%n fichier(s) modifié(s) depuis le dernier commit", nullptr,
                               static_cast<int>(changes.size())));
    diff_->showReadOnlyText(text, QStringLiteral("diff"));
    diff_->setFirstVisibleLine(firstLine);
}

void DiffView::scrollToSelectedFile()
{
    if (const QListWidgetItem *item = files_->currentItem())
        diff_->setFirstVisibleLine(item->data(lineRole).toInt());
}

void DiffView::applyTheme(const Theme &theme)
{
    diff_->applyTheme(theme);
}
