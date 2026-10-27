// SPDX-License-Identifier: GPL-3.0-only

#include "FileChangeDelegate.h"

#include "git/ChangeKindStyle.h"
#include "git/FilePatch.h"
#include "ui/FileIcons.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QTreeWidgetItem>

#include <array>

namespace {

constexpr qreal rowRatio = 1.95; // row height / text height
constexpr int horizontalPadding = 10;
constexpr int checkSpacing = 8; // between the check box and the file's icon
// As the theme's QCheckBox (ThemeStyle).
constexpr int checkBoxSize = 14;
constexpr qreal checkBoxRadius = 3;
constexpr qreal checkMarkWidth = 1.6;
constexpr float emptyBoxOutlineAlpha = 0.45F;

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
    tile.setAlphaF(0.18F);
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

const QStyle *styleOf(const QStyleOptionViewItem &option)
{
    return option.widget ? option.widget->style() : QApplication::style();
}

// The check box at the left of the row, less its padding.
QRect checkBoxRect(const QStyleOptionViewItem &option)
{
    const QRect row = option.rect.adjusted(horizontalPadding, 0, -horizontalPadding, 0);
    return {QPoint(row.left(), row.center().y() - checkBoxSize / 2), QSize(checkBoxSize, checkBoxSize)};
}

// The check of a checked box, or the dash of a partly checked one, in
// white over the accent.
void paintCheckMark(QPainter *painter, const QRectF &box, Qt::CheckState state)
{
    painter->setPen(QPen(Qt::white, checkMarkWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const auto at = [&box](qreal x, qreal y) {
        return QPointF(box.left() + x * box.width(), box.top() + y * box.height());
    };
    switch (state) {
    case Qt::Unchecked:
        return;
    case Qt::PartiallyChecked:
        painter->drawLine(at(0.28, 0.5), at(0.72, 0.5));
        return;
    case Qt::Checked: {
        const std::array<QPointF, 3> check = {at(0.26, 0.52), at(0.43, 0.69), at(0.75, 0.33)};
        painter->drawPolyline(check.data(), int(check.size()));
        return;
    }
    }
}

// Paints the check box of `index`, drawn as the theme's QCheckBox rather
// than by the style, whose item check ignores the theme; returns the room
// it takes at the left.
int paintCheckBox(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index)
{
    const QVariant data = index.data(Qt::CheckStateRole);
    if (!data.isValid())
        return 0;
    const auto state = data.value<Qt::CheckState>();
    const QPalette &palette = option.palette;
    const bool isFilled = state != Qt::Unchecked;
    const QRectF box = QRectF(checkBoxRect(option)).adjusted(0.5, 0.5, -0.5, -0.5);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (!(option.state & QStyle::State_Enabled))
        painter->setOpacity(0.5);
    // The text's color, dimmed: the theme's border is too faint on some
    // light themes for an empty box.
    QColor outline = palette.color(QPalette::Text);
    outline.setAlphaF(emptyBoxOutlineAlpha);
    painter->setPen(isFilled ? palette.color(QPalette::Accent) : outline);
    painter->setBrush(isFilled ? palette.color(QPalette::Accent) : palette.color(QPalette::Base));
    painter->drawRoundedRect(box, checkBoxRadius, checkBoxRadius);
    paintCheckMark(painter, box, state);
    painter->restore();
    return checkBoxSize + checkSpacing;
}

void paintFolder(const Row &row, const QString &label)
{
    // The tree's icon size: the height of its text (TreeMetrics).
    const QSize iconSize = row.option.decorationSize;
    const QRect icon(QPoint(row.rect.left(), row.rect.center().y() - iconSize.height() / 2), iconSize);
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
    setStats(item, patch.addedLines(), patch.removedLines());
}

void FileChangeDelegate::setStats(QTreeWidgetItem *item, int added, int removed)
{
    item->setData(0, addedRole, added);
    item->setData(0, removedRole, removed);
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
    const QStyle *style = styleOf(option);
    Row row{painter, option, option.rect.adjusted(horizontalPadding, 0, -horizontalPadding, 0)};

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // Background, selection and hover from the style; the check box apart.
    const QString label = item.text;
    item.text.clear();
    item.icon = QIcon();
    item.features &= ~QStyleOptionViewItem::HasCheckIndicator;
    style->drawControl(QStyle::CE_ItemViewItem, &item, painter, widget);
    row.rect.setLeft(row.rect.left() + paintCheckBox(painter, option, index));
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

    const QSize iconSize = option.decorationSize;
    int x = row.rect.left();
    FileIcons::file(name, isDark(option.palette))
        .paint(painter, QRect(QPoint(x, row.rect.center().y() - iconSize.height() / 2), iconSize));
    x += iconSize.width() + 8;

    int right = int(paintBadge(row, kind)) - 10;
    // The lines added and removed, when known.
    if (const QVariant added = index.data(addedRole); added.isValid())
        right = paintStats(row, added.toInt(), index.data(removedRole).toInt(), right);

    paintNameAndFolder(row, QRect(x, row.rect.top(), right - x, row.rect.height()), name, folder);
    painter->restore();
}

bool FileChangeDelegate::editorEvent(QEvent *event,
                                     QAbstractItemModel *model,
                                     const QStyleOptionViewItem &option,
                                     const QModelIndex &index)
{
    const QVariant state = index.data(Qt::CheckStateRole);
    const bool isMouseEvent = event->type() == QEvent::MouseButtonPress ||
                              event->type() == QEvent::MouseButtonRelease ||
                              event->type() == QEvent::MouseButtonDblClick;
    if (!state.isValid() || !isMouseEvent)
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    const auto *mouse = static_cast<QMouseEvent *>(event);
    const bool isOnTheBox = checkBoxRect(option).contains(mouse->position().toPoint());
    if (!isOnTheBox || mouse->button() != Qt::LeftButton)
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    // The whole click is the box's: the row's selection stays as it is. On
    // the press, which a quick second click on another box turns into a
    // double-click.
    if (event->type() != QEvent::MouseButtonRelease) {
        const bool isChecked = state.value<Qt::CheckState>() == Qt::Checked;
        model->setData(index, isChecked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
    }
    return true;
}
