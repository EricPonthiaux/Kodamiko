// SPDX-License-Identifier: GPL-3.0-only

#include "StashesPopup.h"

#include "DiffView.h"
#include "GitActions.h"
#include "SnapshotsPopup.h"
#include "ui/Paragraph.h"
#include "ui/PopupWidgets.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

constexpr const char *stashRules = R"(
QListWidget#stashList { background: transparent; border: none; border-right: 1px solid @line; outline: 0; }
QListWidget#stashList::item { padding: 6px 8px; border-radius: 2px; }
QListWidget#stashList::item:hover { background: @hover; }
QListWidget#stashList::item:selected { background: @selection; color: @accentText; }
QPushButton:default:disabled { color: @muted; background: @panel; border: 1px solid @line; }
)";

// "added", "deleted" or "modified", from the header of a file's patch.
QString fileStatus(const QByteArray &chunk)
{
    if (chunk.contains("\nnew file mode"))
        return StashesPopup::tr("added");
    if (chunk.contains("\ndeleted file mode"))
        return StashesPopup::tr("deleted");
    return StashesPopup::tr("modified");
}

} // namespace

StashesPopup::StashesPopup(GitActions *actions, const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("stashes"), QSizeF(0.7, 0.75), QSize(860, 540), parent)
    , theme_(theme)
    , actions_(actions)
    , header_(new PopupHeader(QStringLiteral("files"), tr("Stashes"), this))
    , body_(new QStackedWidget(this))
    , list_(new QListWidget(this))
    , diff_(new DiffView(theme, this))
    , emptyText_(new QLabel(this))
    , drop_(new QPushButton(tr("Drop"), this))
    , apply_(new QPushButton(tr("Apply"), this))
    , pop_(new QPushButton(tr("Pop"), this))
{
    header_->setSubtitle(tr("Changes put aside, to bring back on this branch or another one."));
    connect(header_, &PopupHeader::closeRequested, this, &OverlayPopup::dismiss);
    auto *stashChanges = new QPushButton(tr("Stash Changes…"), this);
    connect(stashChanges, &QPushButton::clicked, this, [this] {
        dismiss();
        actions_->stashChanges();
    });
    header_->addButton(stashChanges);

    list_->setObjectName(QStringLiteral("stashList"));
    list_->setTextElideMode(Qt::ElideRight);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(list_, &QListWidget::currentRowChanged, this, &StashesPopup::showSelected);
    connect(list_, &QListWidget::itemActivated, this,
            [this] { act([this](const Stash &stash) { actions_->applyStash(stash.index, true); }); });
    auto *columns = new QWidget(this);
    auto *columnsLayout = new QHBoxLayout(columns);
    columnsLayout->setContentsMargins(16, 12, 16, 12);
    columnsLayout->setSpacing(12);
    columnsLayout->addWidget(list_, 4);
    columnsLayout->addWidget(diff_, 7);
    body_->addWidget(columns);
    emptyText_->setObjectName(QStringLiteral("pageDescription"));
    emptyText_->setAlignment(Qt::AlignCenter);
    body_->addWidget(emptyText_);

    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("popupFooter"));
    auto *hint = new QLabel(tr("Pop brings the changes back and drops the stash; Apply keeps it."), footer);
    hint->setObjectName(QStringLiteral("footerHint"));
    pop_->setDefault(true);
    connect(drop_, &QPushButton::clicked, this,
            [this] { act([this](const Stash &stash) { actions_->dropStash(stash.index, stash.message); }); });
    connect(apply_, &QPushButton::clicked, this,
            [this] { act([this](const Stash &stash) { actions_->applyStash(stash.index, false); }); });
    connect(pop_, &QPushButton::clicked, this,
            [this] { act([this](const Stash &stash) { actions_->applyStash(stash.index, true); }); });
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 10, 16, 10);
    footerLayout->setSpacing(8);
    footerLayout->addWidget(hint, 1);
    footerLayout->addWidget(drop_);
    footerLayout->addWidget(apply_);
    footerLayout->addWidget(pop_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addWidget(body_, 1);
    layout->addWidget(footer);

    applyTheme(theme);
}

void StashesPopup::applyTheme(const Theme &theme)
{
    theme_ = theme;
    setStyleSheet(popupStyleSheet(theme, QString::fromLatin1(stashRules)));
    header_->applyTheme(theme);
    diff_->applyTheme(theme);
}

void StashesPopup::popup(const QString &folder)
{
    load(folder);
    showCentered();
    list_->setFocus();
}

void StashesPopup::load(const QString &folder)
{
    workDir_.clear();
    stashes_.clear();
    QString error;
    try {
        if (const std::optional<RepositoryInfo> repository = RepositoryInfo::read(folder)) {
            workDir_ = repository->workDir;
            stashes_ = Stashes::list(workDir_);
        } else {
            error = tr("The folder is not in a git repository.");
        }
    } catch (const GitError &exception) {
        error = exception.message();
    }

    const QSignalBlocker blocker(list_);
    list_->clear();
    const QDateTime now = QDateTime::currentDateTime();
    for (const Stash &stash : std::as_const(stashes_)) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1\n%2").arg(stash.message, SnapshotsPopup::describeDate(stash.date, now)), list_);
        item->setToolTip(stash.message);
    }
    body_->setCurrentIndex(stashes_.isEmpty() ? 1 : 0);
    for (QPushButton *button : {drop_, apply_, pop_})
        button->setEnabled(!stashes_.isEmpty());
    if (stashes_.isEmpty()) {
        Paragraph::setText(emptyText_,
                           error.isEmpty() ? tr("No stashes. Stash Changes puts your current changes aside.") : error);
        return;
    }
    list_->setCurrentRow(0);
    showSelected();
}

void StashesPopup::showSelected()
{
    const int row = list_->currentRow();
    if (row < 0 || row >= stashes_.size())
        return;
    try {
        diff_->setFiles(splitPatch(Stashes::patch(workDir_, stashes_.at(row).index)));
    } catch (const GitError &error) {
        diff_->showMessage(error.message());
    }
}

QList<DiffFile> StashesPopup::splitPatch(const QByteArray &patch)
{
    QList<DiffFile> files;
    QList<QByteArray> chunks;
    for (const QByteArray &line : patch.split('\n')) {
        if (line.startsWith("diff --git ") || chunks.isEmpty())
            chunks.append(QByteArray());
        chunks.last() += line + '\n';
    }
    for (const QByteArray &chunk : std::as_const(chunks)) {
        if (!chunk.startsWith("diff --git "))
            continue;
        const QByteArray firstLine = chunk.left(chunk.indexOf('\n'));
        const qsizetype newPath = firstLine.lastIndexOf(" b/");
        DiffFile file;
        file.path = QString::fromUtf8(newPath < 0 ? firstLine.mid(11) : firstLine.mid(newPath + 3));
        file.status = fileStatus(chunk);
        file.patch = FilePatch::parse(chunk);
        files.append(file);
    }
    return files;
}

void StashesPopup::act(const std::function<void(const Stash &)> &action)
{
    const int row = list_->currentRow();
    if (row < 0 || row >= stashes_.size())
        return;
    const Stash stash = stashes_.at(row);
    dismiss();
    action(stash);
}

void StashesPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}
