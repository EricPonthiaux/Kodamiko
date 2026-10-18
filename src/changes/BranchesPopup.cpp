#include "BranchesPopup.h"

#include "GitActions.h"
#include "SnapshotsPopup.h"
#include "ui/AppFonts.h"
#include "ui/PopupWidgets.h"
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

enum Role {
    IndexRole = Qt::UserRole,
    SubtitleRole,
    TrackingRole,
    HeadingRole,
    CurrentRole
};

// The popup's own rules, besides the shared ones (popupStyleSheet).
const char *branchesRules = R"(
QLineEdit#branchFilter { padding: 7px 10px; border-radius: 8px; }
QListWidget#branchList { background: transparent; border: none; outline: 0; }
QLabel#branchesEmpty { color: @muted; }
QPushButton:default:disabled { color: @muted; background: @panel; border: 1px solid @line; }
)";

// A branch's row: the branch icon on a tinted tile, its name (with a
// "current" badge), and below its last commit; on the right, how far it
// is from its upstream. A heading row ("LOCAL", "REMOTE") is small caps.
class BranchDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    Theme theme;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (index.data(HeadingRole).toBool())
            return {0, option.fontMetrics.height() + 14};
        return {0, option.fontMetrics.height() * 2 + 22};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF row = QRectF(option.rect).adjusted(4, 1, -4, -1);
        if (index.data(HeadingRole).toBool()) {
            QFont heading = option.font;
            heading.setPointSizeF(heading.pointSizeF() * 0.85);
            heading.setWeight(QFont::DemiBold);
            painter->setFont(heading);
            painter->setPen(theme.mutedText);
            painter->drawText(row.adjusted(12, 4, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                              index.data(Qt::DisplayRole).toString().toUpper());
            painter->restore();
            return;
        }

        const bool selected = option.state & QStyle::State_Selected;
        if (selected || option.state & QStyle::State_MouseOver) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? theme.selection : theme.hover);
            painter->drawRoundedRect(row, 8, 8);
        }

        const QRectF tile(row.left() + 10, row.center().y() - 14, 28, 28);
        QColor tint = theme.accent;
        tint.setAlpha(theme.isDark ? 46 : 30);
        painter->setPen(Qt::NoPen);
        painter->setBrush(tint);
        painter->drawRoundedRect(tile, 8, 8);
        UiIcons::icon(QStringLiteral("branch"), theme.accent).paint(painter, tile.toRect().adjusted(7, 7, -7, -7));

        // Tracking, at the right.
        const QString tracking = index.data(TrackingRole).toString();
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
        painter->setPen(selected ? theme.accentText : theme.foreground);
        painter->drawText(QRectF(text.left(), top, text.width(), lineHeight), Qt::AlignLeft | Qt::AlignVCenter, name);
        if (index.data(CurrentRole).toBool()) {
            const QString badge = BranchesPopup::tr("current");
            QFont small = option.font;
            small.setPointSizeF(small.pointSizeF() * 0.85);
            const QFontMetrics smallMetrics(small);
            const QRectF pill(text.left() + titleMetrics.horizontalAdvance(name) + 8, top + 1,
                              smallMetrics.horizontalAdvance(badge) + 14, lineHeight - 2);
            painter->setPen(Qt::NoPen);
            painter->setBrush(tint);
            painter->drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
            painter->setFont(small);
            painter->setPen(theme.accent);
            painter->drawText(pill, Qt::AlignCenter, badge);
        }
        painter->setFont(option.font);
        painter->setPen(theme.mutedText);
        painter->drawText(
            QRectF(text.left(), top + lineHeight + 3, text.width(), lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
            option.fontMetrics.elidedText(index.data(SubtitleRole).toString(), Qt::ElideRight, int(text.width())));
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

} // namespace

BranchesPopup::BranchesPopup(GitActions *actions, const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("branches"), QSizeF(0.5, 0.66), QSize(640, 460), parent)
    , theme_(theme)
    , actions_(actions)
    , headerIcon_(new QLabel(this))
    , subtitle_(new QLabel(this))
    , filter_(new QLineEdit(this))
    , list_(new QListWidget(this))
    , empty_(new QLabel(this))
    , merge_(new QPushButton(tr("Merge into Current"), this))
    , rebase_(new QPushButton(tr("Rebase Current onto It"), this))
    , switch_(new QPushButton(tr("Switch"), this))
{
    auto *title = new QLabel(tr("Branches"), this);
    AppFonts::setInterfaceFont(title, 1.5);
    subtitle_->setObjectName(QStringLiteral("pageDescription"));
    subtitle_->setWordWrap(true);
    auto *heading = new QVBoxLayout;
    heading->setSpacing(4);
    heading->addWidget(title);
    heading->addWidget(subtitle_);
    auto *newBranch = new QPushButton(tr("New Branch…"), this);
    connect(newBranch, &QPushButton::clicked, this, [this] {
        dismiss();
        emit newBranchRequested();
    });
    auto *header = new QHBoxLayout;
    header->setContentsMargins(28, 22, 28, 0);
    header->setSpacing(14);
    header->addWidget(headerIcon_, 0, Qt::AlignTop);
    header->addLayout(heading, 1);
    header->addWidget(newBranch, 0, Qt::AlignTop);

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
    empty_->setWordWrap(true);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("settingsCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(4, 6, 4, 6);
    cardLayout->addWidget(list_);
    cardLayout->addWidget(empty_);

    auto *body = new QVBoxLayout;
    body->setContentsMargins(28, 0, 28, 18);
    body->setSpacing(10);
    body->addWidget(filter_);
    body->addWidget(card, 1);

    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("popupFooter"));
    auto *hint = new QLabel(tr("Right-click a branch for more."), footer);
    hint->setObjectName(QStringLiteral("footerHint"));
    switch_->setDefault(true);
    connect(merge_, &QPushButton::clicked, this,
            [this] { act([this](const BranchInfo &branch) { actions_->merge(branch.name); }); });
    connect(rebase_, &QPushButton::clicked, this,
            [this] { act([this](const BranchInfo &branch) { actions_->rebase(branch.name); }); });
    connect(switch_, &QPushButton::clicked, this, &BranchesPopup::switchToSelected);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(28, 12, 18, 14);
    footerLayout->setSpacing(8);
    footerLayout->addWidget(hint, 1);
    footerLayout->addWidget(merge_);
    footerLayout->addWidget(rebase_);
    footerLayout->addWidget(switch_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addSpacing(16);
    layout->addLayout(body, 1);
    layout->addWidget(footer);

    applyTheme(theme);
}

void BranchesPopup::applyTheme(const Theme &theme)
{
    theme_ = theme;
    setStyleSheet(popupStyleSheet(theme, QString::fromLatin1(branchesRules)));
    headerIcon_->setPixmap(iconTile(QStringLiteral("branch"), theme, 40, devicePixelRatioF()));
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
        subtitle_->setText(error.isEmpty() ? tr("The folder is not in a git repository.") : error);
    } else if (repository->detached) {
        subtitle_->setText(tr("HEAD is detached at %1.").arg(repository->headId));
    } else {
        current_ = repository->branch;
        QString text = tr("On %1").arg(repository->branch);
        if (!repository->upstream.isEmpty()) {
            text += tr(", tracking %1").arg(repository->upstream);
            if (repository->ahead == 0 && repository->behind == 0)
                text += tr(" (up to date as of the last fetch)");
            else
                text += tr(" (%1 ahead, %2 behind)").arg(repository->ahead).arg(repository->behind);
        }
        subtitle_->setText(text + QLatin1Char('.'));
    }
    empty_->setText(error.isEmpty() ? tr("No branches yet: make a first commit.") : error);

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
            auto *heading = new QListWidgetItem(branch.remote ? tr("Remote") : tr("Local"), list_);
            heading->setData(HeadingRole, true);
            heading->setFlags(Qt::NoItemFlags);
        }
        auto *item = new QListWidgetItem(branch.name, list_);
        item->setData(IndexRole, i);
        item->setData(SubtitleRole, subtitleOf(branch));
        item->setData(TrackingRole, trackingText(branch));
        item->setData(CurrentRole, branch.current);
        item->setToolTip(branch.subject);
        // The first branch one would switch to: not the current one.
        if (firstSelectable < 0 && !branch.current)
            firstSelectable = list_->count() - 1;
    }
    list_->setVisible(list_->count() > 0);
    empty_->setVisible(list_->count() == 0);
    if (list_->count() == 0 && !branches_.isEmpty())
        empty_->setText(tr("No branch matches \"%1\".").arg(filter));
    if (firstSelectable >= 0)
        list_->setCurrentRow(firstSelectable);
    else if (list_->count() > 1)
        list_->setCurrentRow(1);
    updateButtons();
}

const BranchInfo *BranchesPopup::selected() const
{
    const QListWidgetItem *item = list_->currentItem();
    if (!item || item->data(HeadingRole).toBool())
        return nullptr;
    const int index = item->data(IndexRole).toInt();
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
        menu.addAction(tr("Switch to %1").arg(branch->remote ? Branches::nameOnRemote(branch->name) : branch->name),
                       this, &BranchesPopup::switchToSelected);
        if (onBranch) {
            menu.addAction(tr("Merge into %1").arg(current_), this,
                           [this] { act([this](const BranchInfo &chosen) { actions_->merge(chosen.name); }); });
            menu.addAction(tr("Rebase %1 onto It").arg(current_), this,
                           [this] { act([this](const BranchInfo &chosen) { actions_->rebase(chosen.name); }); });
        }
    }
    if (branch->remote && onBranch) {
        menu.addAction(tr("Track It from %1").arg(current_), this,
                       [this] { act([this](const BranchInfo &chosen) { actions_->trackBranch(chosen.name); }); });
    }
    menu.addSeparator();
    if (!branch->remote) {
        menu.addAction(tr("Rename…"), this,
                       [this] { act([this](const BranchInfo &chosen) { actions_->renameBranch(chosen.name); }); });
    }
    QAction *remove = menu.addAction(branch->remote ? tr("Delete on the Remote…") : tr("Delete"), this, [this] {
        act([this](const BranchInfo &chosen) { actions_->deleteBranch(chosen.name, chosen.remote); });
    });
    remove->setEnabled(!branch->current);
    menu.exec(list_->viewport()->mapToGlobal(position));
}

bool BranchesPopup::eventFilter(QObject *watched, QEvent *event)
{
    // Up and Down in the filter move through the list.
    if (watched == filter_ && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down || key->key() == Qt::Key_PageUp ||
            key->key() == Qt::Key_PageDown) {
            int row = list_->currentRow();
            const int step = key->key() == Qt::Key_Up || key->key() == Qt::Key_PageUp ? -1 : 1;
            do {
                row += step;
            } while (row >= 0 && row < list_->count() && list_->item(row)->data(HeadingRole).toBool());
            if (row >= 0 && row < list_->count())
                list_->setCurrentRow(row);
            return true;
        }
    }
    return OverlayPopup::eventFilter(watched, event);
}

void BranchesPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}
