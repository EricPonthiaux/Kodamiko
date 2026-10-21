// SPDX-License-Identifier: GPL-3.0-only

#include "FileChangeDelegate.h"

#include "git/ChangeKindStyle.h"
#include "git/FilePatch.h"
#include "ui/FileIcons.h"

#include <QApplication>
#include <QPainter>
#include <QTreeWidgetItem>

namespace {

constexpr qreal rowRatio = 1.95; // row height / text height
constexpr int horizontalPadding = 10;

bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Base).lightness() < 128;
}

QColor muted(const QStyleOptionViewItem &option)
{
    QColor color = option.palette.color(QPalette::Text);
    color.setAlphaF(0.5);
    return color;
}

// A row being painted. A selected row is slate blue (see ThemeStyle): the
// text and the git colors stay as they are.
struct Row {
    QPainter *painter = nullptr;
    const QStyleOptionViewItem &option;
    QRect rect; // the row less its padding
};

// The folder shown after the name; a renamed file adds where it was.
QString folderLabel(const QString &path, ChangeKind kind, const QString &oldPath)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    QString folder = slash >= 0 ? path.left(slash) : QString();
    if (kind != ChangeKind::Renamed)
        return folder;
    if (folder.isEmpty())
        return QStringLiteral("← %1").arg(oldPath);
    return folder + QStringLiteral("  ← %1").arg(oldPath);
}

// The badge at the right: a soft tile of the change's color. Returns where
// it starts.
qreal paintBadge(const Row &row, ChangeKind kind)
{
    QPainter *painter = row.painter;
    const int badge = QFontMetrics(row.option.font).height();
    const QRectF badgeRect(row.rect.right() - badge, row.rect.center().y() - badge / 2.0, badge, badge);
    QColor tile = changeKindColor(kind);
    tile.setAlphaF(0.18);
    painter->setPen(Qt::NoPen);
    painter->setBrush(tile);
    painter->drawRoundedRect(badgeRect, 2, 2);
    QFont badgeFont = row.option.font;
    badgeFont.setWeight(QFont::Bold);
    badgeFont.setPointSizeF(row.option.font.pointSizeF() * 0.8);
    painter->setFont(badgeFont);
    painter->setPen(changeKindColor(kind));
    painter->drawText(badgeRect, Qt::AlignCenter, changeKindLetter(kind));
    return badgeRect.left();
}

QString countLabel(const QString &sign, int count)
{
    return count > 0 ? sign + QString::number(count) : QString();
}

// "+12 −3", ending at `right`, in two columns of fixed width (room for
// "−9999"): the counts line up from row to row. Returns where they start.
int paintStats(const Row &row, int added, int removed, int right)
{
    QFont statsFont = row.option.font;
    statsFont.setPointSizeF(row.option.font.pointSizeF() * 0.86);
    statsFont.setWeight(QFont::DemiBold);
    const QFontMetrics statsMetrics(statsFont);
    const int column = statsMetrics.horizontalAdvance(QStringLiteral("−9999"));
    const bool dark = isDark(row.option.palette);
    const QColor red = dark ? QColor("#f85149") : QColor("#cf222e");
    const QColor green = dark ? QColor("#3fb950") : QColor("#1a7f37");
    row.painter->setFont(statsFont);
    for (const auto &[text, color] : {std::pair{countLabel(QStringLiteral("−"), removed), red},
                                      std::pair{countLabel(QStringLiteral("+"), added), green}}) {
        row.painter->setPen(color);
        row.painter->drawText(QRect(right - column, row.rect.top(), column, row.rect.height()),
                              Qt::AlignRight | Qt::AlignVCenter, text);
        right -= column + 4;
    }
    return right - 4;
}

// Name, then the folder, dimmed, cut short if needed, within `area`.
void paintNameAndFolder(const Row &row, const QRect &area, const QString &name, const QString &folder)
{
    QPainter *painter = row.painter;
    const QFontMetrics metrics(row.option.font);
    const int available = area.width();
    painter->setFont(row.option.font);
    painter->setPen(row.option.palette.color(QPalette::Text));
    const QString shownName = metrics.elidedText(name, Qt::ElideMiddle, available);
    painter->drawText(area, Qt::AlignLeft | Qt::AlignVCenter, shownName);
    const int nameWidth = metrics.horizontalAdvance(shownName) + 10;
    const bool folderFits = !folder.isEmpty() && available - nameWidth > 20;
    if (!folderFits)
        return;
    QFont small = row.option.font;
    small.setPointSizeF(row.option.font.pointSizeF() * 0.92);
    painter->setFont(small);
    painter->setPen(muted(row.option));
    const QFontMetrics smallMetrics(small);
    painter->drawText(QRect(area.left() + nameWidth, area.top(), available - nameWidth, area.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      smallMetrics.elidedText(folder, Qt::ElideLeft, available - nameWidth));
}

void paintFolder(const Row &row, const QString &label)
{
    const int iconSize = 16;
    const QRect icon(row.rect.left(), row.rect.center().y() - iconSize / 2, iconSize, iconSize);
    FileIcons::folder(isDark(row.option.palette)).paint(row.painter, icon);
    const QRect text(icon.right() + 9, row.rect.top(), row.rect.right() - icon.right() - 9, row.rect.height());
    row.painter->setFont(row.option.font);
    row.painter->setPen(row.option.palette.color(QPalette::Text));
    row.painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(row.option.font).elidedText(label, Qt::ElideMiddle, text.width()));
}

} // namespace

FileChangeDelegate::FileChangeDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void FileChangeDelegate::setChange(QTreeWidgetItem *item, const FileChange &change)
{
    item->setText(0, changeLabel(change));
    item->setData(0, pathRole, change.path);
    item->setData(0, kindRole, static_cast<int>(change.kind));
    item->setData(0, oldPathRole, change.oldPath);
    item->setToolTip(0, changeLabel(change) + QLatin1Char('\n') + changeStatusText(change));
}

void FileChangeDelegate::setStats(QTreeWidgetItem *item, const FilePatch &patch)
{
    item->setData(0, addedRole, patch.addedLines());
    item->setData(0, removedRole, patch.removedLines());
}

void FileChangeDelegate::setFolder(QTreeWidgetItem *item, const QString &label, const QString &path)
{
    item->setText(0, label);
    item->setData(0, folderRole, path);
    item->setToolTip(0, path);
}

bool FileChangeDelegate::isFolder(const QTreeWidgetItem *item)
{
    return item->data(0, folderRole).isValid();
}

QSize FileChangeDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const QFontMetrics metrics(option.font);
    return {QStyledItemDelegate::sizeHint(option, index).width(), qRound(metrics.height() * rowRatio)};
}

void FileChangeDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem item = option;
    initStyleOption(&item, index);
    const QWidget *widget = option.widget;
    const QStyle *style = widget ? widget->style() : QApplication::style();
    const Row row{painter, option, option.rect.adjusted(horizontalPadding, 0, -horizontalPadding, 0)};

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // Background, selection and hover from the style.
    const QString label = item.text;
    item.text.clear();
    item.icon = QIcon();
    style->drawControl(QStyle::CE_ItemViewItem, &item, painter, widget);
    if (index.data(folderRole).isValid()) {
        paintFolder(row, label);
        painter->restore();
        return;
    }

    const QString path = index.data(pathRole).toString();
    const auto kind = static_cast<ChangeKind>(index.data(kindRole).toInt());
    const QString name = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
    // Under a folder row, the folder is said already.
    const bool isUnderAFolder = index.parent().data(folderRole).isValid();
    const QString folder = folderLabel(isUnderAFolder ? name : path, kind, index.data(oldPathRole).toString());

    const int iconSize = 16;
    int x = row.rect.left();
    FileIcons::file(name, isDark(option.palette))
        .paint(painter, QRect(x, row.rect.center().y() - iconSize / 2, iconSize, iconSize));
    x += iconSize + 8;

    int right = int(paintBadge(row, kind)) - 10;
    // The lines added and removed, when known.
    if (const QVariant added = index.data(addedRole); added.isValid())
        right = paintStats(row, added.toInt(), index.data(removedRole).toInt(), right);

    paintNameAndFolder(row, QRect(x, row.rect.top(), right - x, row.rect.height()), name, folder);
    painter->restore();
}
