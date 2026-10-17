#include "SnapshotsPopup.h"

#include "git/GitCommands.h"
#include "ui/AppFonts.h"
#include "ui/PopupWidgets.h"
#include "ui/UiIcons.h"
#include "workspace/FileIcons.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {

enum Role { SubtitleRole = Qt::UserRole, IconRole };

// The popup's own rules, besides the shared ones (popupStyleSheet).
const char *snapshotRules = R"(
QListWidget#snapshotList, QListWidget#snapshotFiles { background: transparent; border: none; outline: 0; }
QListWidget#snapshotFiles::item { padding: 3px 4px; border-radius: 5px; }
QListWidget#snapshotFiles::item:hover { background: @hover; }
QListWidget#snapshotFiles::item:selected { background: @selection; color: @text; }
QLabel#snapshotBadge { color: @accentText; background: @selection; border-radius: 9px; padding: 2px 10px; }
QLabel#snapshotCommit { color: @muted; }
QLabel#snapshotDate { color: @muted; }
QPushButton:default:disabled { color: @muted; background: @panel; border: 1px solid @line; }
)";

// Row of a snapshot: an icon on a tinted tile, the reason, and below, when
// and how many files.
class SnapshotDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    Theme theme;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return {0, option.fontMetrics.height() * 2 + 26};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF row = QRectF(option.rect).adjusted(4, 2, -4, -2);
        const bool selected = option.state & QStyle::State_Selected;
        if (selected || option.state & QStyle::State_MouseOver) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? theme.selection : theme.hover);
            painter->drawRoundedRect(row, 8, 8);
        }

        // The tile.
        const QRectF tile(row.left() + 10, row.center().y() - 15, 30, 30);
        QColor tint = theme.accent;
        tint.setAlpha(theme.isDark ? 46 : 30);
        painter->setPen(Qt::NoPen);
        painter->setBrush(tint);
        painter->drawRoundedRect(tile, 8, 8);
        const QIcon icon = UiIcons::icon(index.data(IconRole).toString(), theme.accent);
        icon.paint(painter, tile.toRect().adjusted(7, 7, -7, -7));

        // The text.
        const QRectF text(tile.right() + 12, row.top(), row.right() - tile.right() - 22, row.height());
        QFont title = option.font;
        title.setWeight(QFont::DemiBold);
        const QFontMetrics titleMetrics(title);
        const qreal lineHeight = option.fontMetrics.height();
        const qreal top = text.center().y() - lineHeight - 1;
        painter->setFont(title);
        painter->setPen(selected ? theme.accentText : theme.foreground);
        painter->drawText(QRectF(text.left(), top, text.width(), lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
                          titleMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
                                                  int(text.width())));
        painter->setFont(option.font);
        painter->setPen(theme.mutedText);
        painter->drawText(QRectF(text.left(), top + lineHeight + 3, text.width(), lineHeight),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          option.fontMetrics.elidedText(index.data(SubtitleRole).toString(), Qt::ElideRight,
                                                        int(text.width())));
        painter->restore();
    }
};

QString iconFor(const Snapshot &snapshot)
{
    if (snapshot.wholeWorkingTree)
        return QStringLiteral("claude"); // taken before a prompt
    if (snapshot.reason.startsWith(QLatin1String("Before restoring")))
        return QStringLiteral("history");
    return QStringLiteral("discard");
}

QString countText(const Snapshot &snapshot)
{
    if (snapshot.wholeWorkingTree)
        return SnapshotsPopup::tr("Whole directory");
    return SnapshotsPopup::tr("%n file(s)", nullptr, int(snapshot.paths.size()));
}

QLabel *wrapped(const QString &objectName, QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(objectName);
    label->setWordWrap(true);
    return label;
}

} // namespace

SnapshotsPopup::SnapshotsPopup(GitCommands *commands, const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("snapshots"), QSizeF(0.62, 0.72), QSize(820, 520), parent)
    , theme_(theme)
    , commands_(commands)
    , body_(new QStackedWidget(this))
    , headerIcon_(new QLabel(this))
    , list_(new QListWidget(this))
{
    // The header: an icon, the title and what snapshots are.
    auto *title = new QLabel(tr("Restore a Snapshot"), this);
    AppFonts::setInterfaceFont(title, 1.5);
    auto *description = wrapped(QStringLiteral("pageDescription"), this);
    description->setText(tr("Duet keeps a copy of your files before discarding changes, and of the whole working "
                            "directory before each Claude prompt."));
    auto *heading = new QVBoxLayout;
    heading->setSpacing(4);
    heading->addWidget(title);
    heading->addWidget(description);
    auto *header = new QHBoxLayout;
    header->setContentsMargins(28, 22, 28, 0);
    header->setSpacing(14);
    header->addWidget(headerIcon_, 0, Qt::AlignTop);
    header->addLayout(heading, 1);

    // The list and the selected snapshot, side by side.
    list_->setObjectName(QStringLiteral("snapshotList"));
    list_->setItemDelegate(new SnapshotDelegate(list_));
    list_->setMouseTracking(true);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(list_, &QListWidget::currentRowChanged, this, &SnapshotsPopup::showDetails);
    auto *listCard = new QFrame(this);
    listCard->setObjectName(QStringLiteral("settingsCard"));
    auto *listLayout = new QVBoxLayout(listCard);
    listLayout->setContentsMargins(4, 6, 4, 6);
    listLayout->addWidget(list_);

    auto *columns = new QWidget(this);
    auto *columnsLayout = new QHBoxLayout(columns);
    columnsLayout->setContentsMargins(28, 0, 28, 18);
    columnsLayout->setSpacing(14);
    columnsLayout->addWidget(listCard, 5);
    columnsLayout->addWidget(createDetails(), 6);
    body_->addWidget(columns);
    body_->addWidget(createEmptyState());

    // The footer.
    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("popupFooter"));
    auto *hint = wrapped(QStringLiteral("footerHint"), footer);
    hint->setText(tr("Your current files are saved first: a restore can itself be undone."));
    auto *close = new QPushButton(tr("Close"), footer);
    restore_ = new QPushButton(tr("Restore These Files"), footer);
    restore_->setDefault(true);
    close->setMinimumWidth(84);
    connect(close, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    connect(restore_, &QPushButton::clicked, this, &SnapshotsPopup::restoreSelected);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(28, 12, 18, 14);
    footerLayout->setSpacing(8);
    footerLayout->addWidget(hint, 1);
    footerLayout->addWidget(close);
    footerLayout->addWidget(restore_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addSpacing(18);
    layout->addWidget(body_, 1);
    layout->addWidget(footer);

    applyTheme(theme);
}

QWidget *SnapshotsPopup::createDetails()
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("settingsCard"));

    title_ = wrapped(QStringLiteral("snapshotTitle"), card);
    AppFonts::setInterfaceFont(title_, 1.12);
    title_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    date_ = wrapped(QStringLiteral("snapshotDate"), card);
    badge_ = new QLabel(card);
    badge_->setObjectName(QStringLiteral("snapshotBadge"));
    explanation_ = wrapped(QStringLiteral("settingsDescription"), card);

    auto *badgeRow = new QHBoxLayout;
    badgeRow->addWidget(badge_);
    badgeRow->addStretch();

    filesHeading_ = static_cast<QLabel *>(sectionHeading(QString()));
    filesHeading_->setParent(card);
    filesHeading_->setContentsMargins(0, 0, 0, 0);
    files_ = new QListWidget(card);
    files_->setObjectName(QStringLiteral("snapshotFiles"));
    files_->setTextElideMode(Qt::ElideMiddle);
    files_->setIconSize(QSize(16, 16));
    files_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    commit_ = new QLabel(card);
    commit_->setObjectName(QStringLiteral("snapshotCommit"));
    commit_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(6);
    layout->addWidget(title_);
    layout->addWidget(date_);
    layout->addSpacing(4);
    layout->addLayout(badgeRow);
    layout->addWidget(explanation_);
    layout->addSpacing(10);
    layout->addWidget(filesHeading_);
    layout->addWidget(files_, 1);
    layout->addWidget(commit_);
    return card;
}

QWidget *SnapshotsPopup::createEmptyState()
{
    auto *page = new QWidget(this);
    emptyIcon_ = new QLabel(page);
    emptyIcon_->setAlignment(Qt::AlignCenter);
    emptyTitle_ = new QLabel(page);
    emptyTitle_->setAlignment(Qt::AlignCenter);
    AppFonts::setInterfaceFont(emptyTitle_, 1.2);
    emptyText_ = wrapped(QStringLiteral("pageDescription"), page);
    emptyText_->setAlignment(Qt::AlignCenter);
    emptyText_->setContentsMargins(60, 0, 60, 0);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 0, 28, 40);
    layout->setSpacing(10);
    layout->addStretch();
    layout->addWidget(emptyIcon_);
    layout->addSpacing(4);
    layout->addWidget(emptyTitle_);
    layout->addWidget(emptyText_);
    layout->addStretch();
    return page;
}

void SnapshotsPopup::applyTheme(const Theme &theme)
{
    theme_ = theme;
    setStyleSheet(popupStyleSheet(theme, QString::fromLatin1(snapshotRules)));
    const qreal ratio = devicePixelRatioF();
    headerIcon_->setPixmap(iconTile(QStringLiteral("history"), theme, 40, ratio));
    emptyIcon_->setPixmap(iconTile(QStringLiteral("history"), theme, 64, ratio));
    QFont mono = AppFonts::codeFont(font().pointSizeF() * 0.9);
    commit_->setFont(mono);
    if (auto *delegate = dynamic_cast<SnapshotDelegate *>(list_->itemDelegate()))
        delegate->theme = theme;
    list_->viewport()->update();
    showDetails(); // file icons for dark or light backgrounds
}

void SnapshotsPopup::popup(const QString &folder)
{
    load(folder);
    showCentered();
    list_->setFocus();
}

void SnapshotsPopup::load(const QString &folder)
{
    folder_ = folder;
    snapshots_.clear();
    QString error;
    try {
        snapshots_ = Snapshots::list(folder_);
    } catch (const GitError &exception) {
        error = QString::fromStdString(exception.what());
    }

    const QSignalBlocker blocker(list_);
    list_->clear();
    const QDateTime now = QDateTime::currentDateTime();
    for (const Snapshot &snapshot : std::as_const(snapshots_)) {
        auto *item = new QListWidgetItem(snapshot.reason, list_);
        item->setData(SubtitleRole, QStringLiteral("%1  ·  %2").arg(describeDate(snapshot.date, now), countText(snapshot)));
        item->setData(IconRole, iconFor(snapshot));
        item->setToolTip(snapshot.reason);
    }

    body_->setCurrentIndex(snapshots_.isEmpty() ? 1 : 0);
    restore_->setEnabled(!snapshots_.isEmpty());
    if (snapshots_.isEmpty()) {
        emptyTitle_->setText(error.isEmpty() ? tr("No snapshots yet") : tr("Snapshots are unavailable"));
        emptyText_->setText(error.isEmpty()
                                ? tr("They appear here when you discard changes in Diff mode, and before each "
                                     "prompt you send to Claude.")
                                : error);
        return;
    }
    list_->setCurrentRow(0);
    showDetails();
}

void SnapshotsPopup::showDetails()
{
    const int row = list_->currentRow();
    if (row < 0 || row >= snapshots_.size())
        return;
    const Snapshot &snapshot = snapshots_.at(row);
    title_->setText(snapshot.reason);
    date_->setText(describeDate(snapshot.date, QDateTime::currentDateTime()));
    date_->setToolTip(QLocale().toString(snapshot.date, QLocale::LongFormat));
    badge_->setText(countText(snapshot));
    explanation_->setText(snapshot.wholeWorkingTree
                              ? tr("Restoring undoes every change made in the working directory since then, "
                                   "ignored files excepted. The files below had changed since the last commit.")
                              : tr("Restoring puts these files back as they were; a file that did not exist yet "
                                   "is deleted. Other files are left alone."));
    filesHeading_->setText((snapshot.wholeWorkingTree ? tr("Changed files · %1") : tr("Files · %1"))
                               .arg(snapshot.paths.size())
                               .toUpper());
    files_->clear();
    for (const QString &path : snapshot.paths) {
        auto *item = new QListWidgetItem(FileIcons::file(QFileInfo(path).fileName(), theme_.isDark), path, files_);
        item->setToolTip(path);
    }
    if (snapshot.paths.isEmpty())
        new QListWidgetItem(tr("(none)"), files_);
    commit_->setText(tr("Commit %1").arg(snapshot.commitId.left(12)));
    commit_->setToolTip(snapshot.commitId);
}

void SnapshotsPopup::restoreSelected()
{
    const int row = list_->currentRow();
    if (row < 0 || row >= snapshots_.size())
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
    dismiss();
}

void SnapshotsPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}

QString SnapshotsPopup::describeDate(const QDateTime &date, const QDateTime &now)
{
    const qint64 seconds = date.secsTo(now);
    if (seconds >= 0 && seconds < 60)
        return tr("Just now");
    if (seconds >= 0 && seconds < 3600)
        return tr("%n min ago", nullptr, int(seconds / 60));
    const QLocale locale;
    const QString time = locale.toString(date.time(), QLocale::ShortFormat);
    const qint64 days = date.date().daysTo(now.date());
    if (days == 0)
        return tr("Today at %1").arg(time);
    if (days == 1)
        return tr("Yesterday at %1").arg(time);
    if (days > 1 && days < 7)
        return tr("%1 at %2").arg(locale.dayName(date.date().dayOfWeek()), time);
    return locale.toString(date, QLocale::ShortFormat);
}
