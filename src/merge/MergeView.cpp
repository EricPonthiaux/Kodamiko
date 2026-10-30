// SPDX-License-Identifier: GPL-3.0-only

#include "MergeView.h"

#include "MergeTextPanel.h"
#include "git/ThreeWayMerge.h"
#include "ui/AppFonts.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr int listWidthPx = 260;
constexpr int barMarginPx = 10;
constexpr int barSpacingPx = 8;

using ConflictVersions::Kind;

bool isText(const ConflictVersions::File &file)
{
    const bool hasBothSides = file.kind == Kind::BothModified || file.kind == Kind::BothAdded;
    return hasBothSides && !file.isBinary;
}

QString pathOf(const std::optional<ConflictVersions::Version> &version)
{
    return version ? version->path : QString();
}

// Empty for a version the file does not have.
QByteArray contentOf(const std::optional<ConflictVersions::Version> &version)
{
    return version ? version->content : QByteArray();
}

} // namespace

MergeView::MergeView(QString folder, Theme theme, QWidget *parent)
    : QWidget(parent)
    , folder_(std::move(folder))
    , theme_(std::move(theme))
    , list_(new QListWidget(this))
    , message_(new QLabel(this))
{
    list_->setFixedWidth(listWidthPx);
    list_->setFrameShape(QFrame::NoFrame);
    connect(list_, &QListWidget::currentRowChanged, this, &MergeView::showFile);
    message_->setObjectName(QStringLiteral("pageDescription"));
    message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true);

    auto *fileArea = new QWidget(this);
    fileArea_ = new QVBoxLayout(fileArea);
    fileArea_->setContentsMargins(0, 0, 0, 0);
    fileArea_->addWidget(message_);

    auto *body = new QHBoxLayout;
    body->setSpacing(0);
    body->addWidget(list_);
    body->addWidget(fileArea, 1);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createBar());
    layout->addLayout(body, 1);
    showMessage(tr("Reading the conflicts…"));
    reload();
}

MergeView::~MergeView() = default;

void MergeView::reload()
{
    pool_.start([this, folder = folder_] {
        Conflicts conflicts;
        try {
            const std::optional<RepositoryInfo> repository = RepositoryInfo::read(folder);
            if (repository) {
                conflicts.repository = *repository;
                conflicts.files = ConflictVersions::read(*repository);
                conflicts.sides = Integration::conflictSides(*repository);
            } else {
                conflicts.error = tr("This folder is not in a git repository.");
            }
        } catch (const GitError &error) {
            conflicts.error = error.message();
        }
        QMetaObject::invokeMethod(this, [this, conflicts] { showConflicts(conflicts); }, Qt::QueuedConnection);
    });
}

void MergeView::applyTheme(const Theme &theme)
{
    theme_ = theme;
    if (panel_)
        panel_->applyTheme(theme);
}

QWidget *MergeView::createBar()
{
    auto *bar = new QWidget(this);
    title_ = new QLabel(bar);
    AppFonts::setInterfaceFont(title_, 1.05, QFont::DemiBold);
    title_->setTextFormat(Qt::PlainText);
    summary_ = new QLabel(bar);
    summary_->setObjectName(QStringLiteral("pageDescription"));
    previous_ = new QPushButton(tr("Previous Conflict"), bar);
    previous_->setToolTip(tr("Shift+F7"));
    next_ = new QPushButton(tr("Next Conflict"), bar);
    next_->setToolTip(tr("F7"));
    connect(previous_, &QPushButton::clicked, this, [this] {
        if (panel_)
            panel_->goToConflict(MergeTextPanel::Direction::Previous);
    });
    connect(next_, &QPushButton::clicked, this, [this] {
        if (panel_)
            panel_->goToConflict(MergeTextPanel::Direction::Next);
    });
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(barMarginPx, barMarginPx, barMarginPx, barMarginPx);
    layout->setSpacing(barSpacingPx);
    layout->addWidget(title_);
    layout->addWidget(summary_);
    layout->addStretch();
    layout->addWidget(previous_);
    layout->addWidget(next_);
    return bar;
}

void MergeView::showConflicts(const Conflicts &conflicts)
{
    conflicts_ = conflicts;
    title_->setText(titleOf(conflicts));
    const QString selected = list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString() : QString();
    {
        const QSignalBlocker block(list_);
        list_->clear();
        for (const ConflictVersions::File &file : std::as_const(conflicts_.files)) {
            auto *item = new QListWidgetItem(list_);
            item->setData(Qt::UserRole, file.path);
            item->setToolTip(file.path);
        }
    }
    updateList();
    if (!conflicts.error.isEmpty()) {
        showMessage(conflicts.error);
    } else if (conflicts_.files.isEmpty()) {
        showMessage(tr("No file is in conflict."));
    } else {
        const auto found = std::ranges::find(conflicts_.files, selected, &ConflictVersions::File::path);
        list_->setCurrentRow(found == conflicts_.files.end() ? 0 : int(found - conflicts_.files.begin()));
    }
    updateBar();
}

void MergeView::showFile(int row)
{
    if (panel_)
        panel_->deleteLater();
    if (row < 0 || row >= conflicts_.files.size())
        return;
    const ConflictVersions::File &file = conflicts_.files[row];
    MergeDocument *document = documentOf(file);
    if (!document) {
        showMessage(tr("%1: %2.\nFor now, resolve it from the Diff view's menu: Accept Current Version or Accept "
                       "Incoming Version.")
                        .arg(file.path, stateOf(file)));
        updateBar();
        return;
    }
    message_->hide();
    panel_ = new MergeTextPanel(theme_, {file.path, document, conflicts_.sides}, this);
    connect(panel_, &MergeTextPanel::documentChanged, this, [this] {
        updateList();
        updateBar();
    });
    fileArea_->addWidget(panel_);
    panel_->setFocus();
    updateList();
    updateBar();
}

void MergeView::showMessage(const QString &message)
{
    message_->setText(message);
    message_->show();
}

void MergeView::updateList()
{
    for (int row = 0; row < list_->count(); ++row) {
        const ConflictVersions::File &file = conflicts_.files[row];
        list_->item(row)->setText(QFileInfo(file.path).fileName() + QLatin1Char('\n') + stateOf(file));
    }
}

void MergeView::updateBar()
{
    int unresolved = 0;
    for (const auto &[path, document] : documents_)
        unresolved += document->unresolvedCount();
    const qsizetype files = conflicts_.files.size();
    summary_->setText(files == 0 ? QString()
                                 : tr("%n file(s) in conflict", nullptr, int(files)) + QStringLiteral(" · ") +
                                       tr("%n conflict(s) left in the files opened", nullptr, unresolved));
    previous_->setEnabled(panel_);
    next_->setEnabled(panel_);
}

QString MergeView::titleOf(const Conflicts &conflicts) const
{
    const QString &operation = conflicts.repository.operation;
    const Integration::ConflictSides &sides = conflicts.sides;
    if (operation == tr("merge"))
        return tr("Merging %1 into %2").arg(sides.incoming, sides.current);
    if (operation == tr("rebase"))
        return tr("Rebasing onto %1: %2").arg(sides.current, sides.incoming);
    if (operation == tr("cherry-pick"))
        return tr("Cherry-picking %1 onto %2").arg(sides.incoming, sides.current);
    if (operation == tr("revert"))
        return tr("%1 on %2").arg(sides.incoming, sides.current);
    return tr("Conflicts on %1").arg(sides.current);
}

QString MergeView::stateOf(const ConflictVersions::File &file) const
{
    const Integration::ConflictSides &sides = conflicts_.sides;
    if (const auto found = documents_.find(file.path); found != documents_.end()) {
        const int left = found->second->unresolvedCount();
        return left == 0 ? tr("Resolved, not saved yet") : tr("%n conflict(s) left", nullptr, left);
    }
    if (file.isBinary)
        return tr("Binary file changed on both sides");
    switch (file.kind) {
    case Kind::BothModified:
        return tr("Changed on both sides");
    case Kind::BothAdded:
        return tr("Added on both sides");
    case Kind::DeletedByCurrent:
        return tr("Deleted on %1, changed on %2").arg(sides.current, sides.incoming);
    case Kind::DeletedByIncoming:
        return tr("Deleted on %1, changed on %2").arg(sides.incoming, sides.current);
    case Kind::RenamedOnBothSides:
        return tr("Renamed %1 on %2 and %3 on %4")
            .arg(pathOf(file.current), sides.current, pathOf(file.incoming), sides.incoming);
    case Kind::DeletedByBoth:
        return tr("Deleted on both sides");
    case Kind::AddedByCurrent:
        return tr("Added on %1 only").arg(sides.current);
    case Kind::AddedByIncoming:
        return tr("Added on %1 only").arg(sides.incoming);
    }
    return {};
}

MergeDocument *MergeView::documentOf(const ConflictVersions::File &file)
{
    if (!isText(file))
        return nullptr;
    std::unique_ptr<MergeDocument> &document = documents_[file.path];
    if (!document) {
        document = std::make_unique<MergeDocument>(
            ThreeWayMerge::merge(contentOf(file.base), contentOf(file.current), contentOf(file.incoming)));
    }
    return document.get();
}
