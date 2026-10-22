// SPDX-License-Identifier: GPL-3.0-only

#include "BranchesPopup.h"

#include "GitActions.h"
#include "SnapshotsPopup.h"
#include "ui/Paragraph.h"
#include "ui/PopupWidgets.h"
#include "ui/ThemeStyle.h"
#include "ui/UiIcons.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {

constexpr int indexRole = Qt::UserRole;
constexpr int subtitleRole = Qt::UserRole + 1;
constexpr int trackingRole = Qt::UserRole + 2;
constexpr int headingRole = Qt::UserRole + 3;
constexpr int currentRole = Qt::UserRole + 4;

// The popup's own rules, besides the shared ones (popupStyleSheet).
constexpr const char *branchesRules = R"(
QListWidget#branchList { background: transparent; border: none; border-top: 1px solid @line; outline: 0; }
QLabel#branchesEmpty { color: @muted; }
QPushButton:default:disabled { color: @muted; background: @panel; border: 1px solid @line; }
)";

// A branch's row: the branch icon, its name (with a "current" badge), and
// below its last commit; on the right, how far it is from its upstream. A
// heading row ("Local", "Remote") is in demibold.
class BranchDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    Theme theme;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (index.data(headingRole).toBool())
            return {0, option.fontMetrics.height() + 12};
        return {0, option.fontMetrics.height() * 2 + 14};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF row = QRectF(option.rect).adjusted(0, 1, 0, -1);
        if (index.data(headingRole).toBool()) {
            QFont heading = option.font;
            heading.setWeight(QFont::DemiBold);
            painter->setFont(heading);
            painter->setPen(theme.mutedText);
            painter->drawText(row.adjusted(8, 4, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                              index.data(Qt::DisplayRole).toString());
            painter->restore();
            return;
        }

        const bool selected = option.state & QStyle::State_Selected;
        if (selected || option.state & QStyle::State_MouseOver) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? ThemeStyle::selectionColor(theme) : theme.hover);
            painter->drawRoundedRect(row, 2, 2);
        }

        // The icon, alone: 16 pixels, in the accent.
        const QRectF tile(row.left() + 8, row.center().y() - 8, 16, 16);
        UiIcons::icon(QStringLiteral("branch"), theme.accent).paint(painter, tile.toRect());

        // Tracking, at the right.
        const QString tracking = index.data(trackingRole).toString();
        const qreal trackingWidth = tracking.isEmpty() ? 0 : option.fontMetrics.horizontalAdvance(tracking) + 16;
        if (!tracking.isEmpty()) {
            painter->setPen(theme.mutedText);
            painter->setFont(option.font);
            painter->drawText(QRectF(row.right() - trackingWidth - 6, row.top(), trackingWidth, row.height()),
                              Qt::AlignRight | Qt::AlignVCenter, tracking);
        }

        const QRectF text(tile.right() + 12, row.top(), row.right() - tile.right() - 22 - trackingWidth, row.height());
        QFont title = option.font;
        title.setWeight(QFont::DemiBold);
        const QFontMetrics titleMetrics(title);
        const qreal lineHeight = option.fontMetrics.height();
        const qreal top = text.center().y() - lineHeight - 1;
        const QString name =
            titleMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, int(text.width()));
        painter->setFont(title);
        painter->setPen(theme.foreground);
        painter->drawText(QRectF(text.left(), top, text.width(), lineHeight), Qt::AlignLeft | Qt::AlignVCenter, name);
        if (index.data(currentRole).toBool()) {
            const QString badge = BranchesPopup::tr("current");
            QFont small = option.font;
            small.setPointSizeF(small.pointSizeF() * 0.85);
            const QFontMetrics smallMetrics(small);
            const QRectF pill(text.left() + titleMetrics.horizontalAdvance(name) + 8, top + 1,
                              smallMetrics.horizontalAdvance(badge) + 14, lineHeight - 2);
            QColor tint = theme.accent;
            tint.setAlpha(theme.isDark ? 46 : 30);
            painter->setPen(Qt::NoPen);
            painter->setBrush(tint);
            painter->drawRoundedRect(pill, 2, 2);
            painter->setFont(small);
            painter->setPen(theme.accent);
            painter->drawText(pill, Qt::AlignCenter, badge);
        }
        painter->setFont(option.font);
        painter->setPen(theme.mutedText);
        painter->drawText(
            QRectF(text.left(), top + lineHeight + 3, text.width(), lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
            option.fontMetrics.elidedText(index.data(subtitleRole).toString(), Qt::ElideRight, int(text.width())));
        painter->restore();
    }
};

// "↑2 ↓1", "gone", or empty when the branch is level with its upstream.
QString trackingText(const BranchInfo &branch)
{
    if (branch.upstreamGone)
        return BranchesPopup::tr("upstream gone");
    QStringList parts;
    if (branch.ahead > 0)
        parts.append(QStringLiteral("↑%1").arg(branch.ahead));
    if (branch.behind > 0)
        parts.append(QStringLiteral("↓%1").arg(branch.behind));
    return parts.join(QLatin1Char(' '));
}

QString subtitleOf(const BranchInfo &branch)
{
    QStringList parts{SnapshotsPopup::describeDate(branch.date, QDateTime::currentDateTime()), branch.subject};
    if (!branch.upstream.isEmpty())
        parts.prepend(QStringLiteral("→ ") + branch.upstream);
    return parts.join(QStringLiteral("  ·  "));
}

// "On main, tracking origin/main (2 ahead, 1 behind).", for a branch.
QString branchSummary(const RepositoryInfo &repository)
{
    QString text = BranchesPopup::tr("On %1").arg(repository.branch);
    if (!repository.upstream.isEmpty()) {
        text += BranchesPopup::tr(", tracking %1").arg(repository.upstream);
        const bool isLevel = repository.ahead == 0 && repository.behind == 0;
        if (isLevel)
            text += BranchesPopup::tr(" (up to date as of the last fetch)");
        else
            text += BranchesPopup::tr(" (%1 ahead, %2 behind)").arg(repository.ahead).arg(repository.behind);
    }
    return text + QLatin1Char('.');
}

void addHeading(QListWidget *list, bool remote)
{
    auto *heading = new QListWidgetItem(remote ? BranchesPopup::tr("Remote") : BranchesPopup::tr("Local"), list);
    heading->setData(headingRole, true);
    heading->setFlags(Qt::NoItemFlags);
}

void addBranch(QListWidget *list, const BranchInfo &branch, int index)
{
    auto *item = new QListWidgetItem(branch.name, list);
    item->setData(indexRole, index);
    item->setData(subtitleRole, subtitleOf(branch));
    item->setData(trackingRole, trackingText(branch));
    item->setData(currentRole, branch.current);
    item->setToolTip(branch.subject);
}

bool isNavigationKey(int key)
{
    return key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp || key == Qt::Key_PageDown;
}

// Moves the current row by `step`, skipping the headings; stays put at the
// ends of the list.
void moveToBranchRow(QListWidget *list, int step)
{
    int row = list->currentRow();
    do {
        row += step;
    } while (row >= 0 && row < list->count() && list->item(row)->data(headingRole).toBool());
    if (row >= 0 && row < list->count())
        list->setCurrentRow(row);
}

} // namespace

BranchesPopup::BranchesPopup(GitActions *actions, const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("branches"), QSizeF(0.5, 0.66), QSize(640, 460), parent)
    , theme_(theme)
    , actions_(actions)
    , header_(new PopupHeader(QStringLiteral("branch"), tr("Branches"), this))
    , filter_(new QLineEdit(this))
    , list_(new QListWidget(this))
    , empty_(new QLabel(this))
    , merge_(new QPushButton(tr("Merge into Current"), this))
    , rebase_(new QPushButton(tr("Rebase Current onto It"), this))
    , switch_(new QPushButton(tr("Switch"), this))
{
    auto *newBranch = new QPushButton(tr("New Branch…"), this);
    connect(newBranch, &QPushButton::clicked, this, [this] {
        dismiss();
        emit newBranchRequested();
    });
    header_->addButton(newBranch);
    connect(header_, &PopupHeader::closeRequested, this, &OverlayPopup::dismiss);

    filter_->setObjectName(QStringLiteral("branchFilter"));
    filter_->setPlaceholderText(tr("Filter branches"));
    filter_->setClearButtonEnabled(true);
    filter_->installEventFilter(this);
    connect(filter_, &QLineEdit::textChanged, this, &BranchesPopup::rebuildList);
    connect(filter_, &QLineEdit::returnPressed, this, &BranchesPopup::switchToSelected);

    list_->setObjectName(QStringLiteral("branchList"));
    list_->setItemDelegate(new BranchDelegate(list_));
    list_->setMouseTracking(true);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_, &QListWidget::currentRowChanged, this, &BranchesPopup::updateButtons);
    connect(list_, &QListWidget::itemActivated, this, &BranchesPopup::switchToSelected);
    connect(list_, &QWidget::customContextMenuRequested, this, &BranchesPopup::showMenu);
    empty_->setObjectName(QStringLiteral("branchesEmpty"));
    empty_->setAlignment(Qt::AlignCenter);

    auto *body = new QVBoxLayout;
    body->setContentsMargins(16, 12, 16, 12);
    body->setSpacing(10);
    body->addWidget(filter_);
    body->addWidget(list_, 1);
    body->addWidget(empty_, 1);

    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("popupFooter"));
    auto *hint = new QLabel(tr("Right-click a branch for more."), footer);
    hint->setObjectName(QStringLiteral("footerHint"));
    switch_->setDefault(true);
    connect(merge_, &QPushButton::clicked, this, &BranchesPopup::mergeSelected);
    connect(rebase_, &QPushButton::clicked, this, &BranchesPopup::rebaseSelected);
    connect(switch_, &QPushButton::clicked, this, &BranchesPopup::switchToSelected);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 10, 16, 10);
    footerLayout->setSpacing(8);
    footerLayout->addWidget(hint, 1);
    footerLayout->addWidget(merge_);
    footerLayout->addWidget(rebase_);
    footerLayout->addWidget(switch_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addLayout(body, 1);
    layout->addWidget(footer);

    applyTheme(theme);
}

void BranchesPopup::applyTheme(const Theme &theme)
{
    theme_ = theme;
    setStyleSheet(popupStyleSheet(theme, QString::fromLatin1(branchesRules)));
    header_->applyTheme(theme);
    if (auto *delegate = dynamic_cast<BranchDelegate *>(list_->itemDelegate()))
        delegate->theme = theme;
    list_->viewport()->update();
}

void BranchesPopup::popup(const QString &folder)
{
    load(folder);
    showCentered();
    filter_->setFocus();
}

void BranchesPopup::load(const QString &folder)
{
    folder_ = folder;
    branches_.clear();
    current_.clear();
    QString error;
    std::optional<RepositoryInfo> repository;
    try {
        repository = RepositoryInfo::read(folder);
        if (repository && !repository->unborn)
            branches_ = Branches::list(repository->workDir);
    } catch (const GitError &exception) {
        error = exception.message();
    }

    if (!repository) {
        header_->setSubtitle(error.isEmpty() ? tr("The folder is not in a git repository.") : error);
    } else if (repository->detached) {
        header_->setSubtitle(tr("HEAD is detached at %1.").arg(repository->headId));
    } else {
        current_ = repository->branch;
        header_->setSubtitle(branchSummary(*repository));
    }
    Paragraph::setText(empty_, error.isEmpty() ? tr("No branches yet: make a first commit.") : error);

    const QSignalBlocker blocker(filter_);
    filter_->clear();
    rebuildList();
}

void BranchesPopup::rebuildList()
{
    const QString filter = filter_->text().trimmed();
    const QSignalBlocker blocker(list_);
    list_->clear();
    bool remoteHeading = false;
    bool localHeading = false;
    int firstSelectable = -1;
    for (int i = 0; i < branches_.size(); ++i) {
        const BranchInfo &branch = branches_.at(i);
        if (!filter.isEmpty() && !branch.name.contains(filter, Qt::CaseInsensitive))
            continue;
        bool &headingShown = branch.remote ? remoteHeading : localHeading;
        if (!headingShown) {
            headingShown = true;
            addHeading(list_, branch.remote);
        }
        addBranch(list_, branch, i);
        // The first branch one would switch to: not the current one.
        if (firstSelectable < 0 && !branch.current)
            firstSelectable = list_->count() - 1;
    }
    list_->setVisible(list_->count() > 0);
    empty_->setVisible(list_->count() == 0);
    if (list_->count() == 0 && !branches_.isEmpty())
        Paragraph::setText(empty_, tr("No branch matches \"%1\".").arg(filter));
    if (firstSelectable >= 0)
        list_->setCurrentRow(firstSelectable);
    else if (list_->count() > 1)
        list_->setCurrentRow(1);
    updateButtons();
}

const BranchInfo *BranchesPopup::selected() const
{
    const QListWidgetItem *item = list_->currentItem();
    if (!item || item->data(headingRole).toBool())
        return nullptr;
    const int index = item->data(indexRole).toInt();
    return index >= 0 && index < branches_.size() ? &branches_.at(index) : nullptr;
}

void BranchesPopup::updateButtons()
{
    const BranchInfo *branch = selected();
    const bool other = branch && !branch->current;
    const bool onBranch = !current_.isEmpty();
    switch_->setEnabled(other);
    merge_->setEnabled(other && onBranch);
    rebase_->setEnabled(other && onBranch);
    if (other) {
        merge_->setToolTip(tr("Merge %1 into %2").arg(branch->name, current_));
        rebase_->setToolTip(tr("Replay the commits of %1 on top of %2").arg(current_, branch->name));
    }
}

void BranchesPopup::act(const std::function<void(const BranchInfo &)> &action)
{
    const BranchInfo *branch = selected();
    if (!branch)
        return;
    const BranchInfo copy = *branch;
    dismiss();
    action(copy);
}

void BranchesPopup::switchToSelected()
{
    const BranchInfo *branch = selected();
    if (!branch || branch->current)
        return;
    act([this](const BranchInfo &chosen) { actions_->switchBranch(chosen.name, chosen.remote); });
}

void BranchesPopup::mergeSelected()
{
    act([this](const BranchInfo &chosen) { actions_->merge(chosen.name); });
}

void BranchesPopup::rebaseSelected()
{
    act([this](const BranchInfo &chosen) { actions_->rebase(chosen.name); });
}

void BranchesPopup::trackSelected()
{
    act([this](const BranchInfo &chosen) { actions_->trackBranch(chosen.name); });
}

void BranchesPopup::renameSelected()
{
    act([this](const BranchInfo &chosen) { actions_->renameBranch(chosen.name); });
}

void BranchesPopup::deleteSelected()
{
    act([this](const BranchInfo &chosen) { actions_->deleteBranch(chosen.name, chosen.remote); });
}

void BranchesPopup::showMenu(const QPoint &position)
{
    if (QListWidgetItem *item = list_->itemAt(position))
        list_->setCurrentItem(item);
    const BranchInfo *branch = selected();
    if (!branch)
        return;
    const bool onBranch = !current_.isEmpty();
    QMenu menu(this);
    if (!branch->current) {
        const QString shortName = branch->remote ? Branches::nameOnRemote(branch->name) : branch->name;
        menu.addAction(tr("Switch to %1").arg(shortName), this, &BranchesPopup::switchToSelected);
        if (onBranch) {
            menu.addAction(tr("Merge into %1").arg(current_), this, &BranchesPopup::mergeSelected);
            menu.addAction(tr("Rebase %1 onto It").arg(current_), this, &BranchesPopup::rebaseSelected);
        }
    }
    if (branch->remote && onBranch)
        menu.addAction(tr("Track It from %1").arg(current_), this, &BranchesPopup::trackSelected);
    menu.addSeparator();
    if (!branch->remote)
        menu.addAction(tr("Rename…"), this, &BranchesPopup::renameSelected);
    QAction *remove = menu.addAction(branch->remote ? tr("Delete on the Remote…") : tr("Delete"), this,
                                     &BranchesPopup::deleteSelected);
    remove->setEnabled(!branch->current);
    menu.exec(list_->viewport()->mapToGlobal(position));
}

bool BranchesPopup::eventFilter(QObject *watched, QEvent *event)
{
    // Up and Down in the filter move through the list.
    const bool isFilterKey = watched == filter_ && event->type() == QEvent::KeyPress;
    if (!isFilterKey)
        return OverlayPopup::eventFilter(watched, event);
    const int key = static_cast<QKeyEvent *>(event)->key();
    if (!isNavigationKey(key))
        return OverlayPopup::eventFilter(watched, event);
    const bool isUpward = key == Qt::Key_Up || key == Qt::Key_PageUp;
    moveToBranchRow(list_, isUpward ? -1 : 1);
    return true;
}

void BranchesPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}
