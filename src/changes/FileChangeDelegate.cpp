#include "FileChangeDelegate.h"

#include "git/ChangeKindStyle.h"
#include "git/FilePatch.h"
#include "workspace/FileIcons.h"

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

QColor muted(const QStyleOptionViewItem &option, bool selected)
{
    QColor color = option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    color.setAlphaF(selected ? 0.75 : 0.5);
    return color;
}

// A row being painted.
struct Row {
    QPainter *painter = nullptr;
    const QStyleOptionViewItem &option;
    QRect rect; // the row less its padding
    // Selected in the active window, the row is on the accent: white text.
    // Otherwise its background is a soft gray and the text keeps its colors.
    bool onAccent = false;
};

// "STAGED  2": small capitals, dimmed, on the row's lower part.
void paintSectionTitle(const Row &row, const QString &title, int count)
{
    QPainter *painter = row.painter;
    QFont font = row.option.font;
    font.setCapitalization(QFont::AllUppercase);
    font.setPointSizeF(font.pointSizeF() * 0.82);
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::PercentageSpacing, 108);
    painter->setFont(font);
    painter->setPen(muted(row.option, row.onAccent));
    const QFontMetrics metrics(font);
    const QRect text(row.rect.left(), row.rect.bottom() - metrics.height() - 4, row.rect.width(), metrics.height());
    painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter, title);
    const QString countText = QString::number(count);
    const int x = row.rect.left() + metrics.horizontalAdvance(title) + 8;
    const QRectF bubble(x, text.center().y() - metrics.height() / 2.0 - 1, metrics.horizontalAdvance(countText) + 12,
                        metrics.height() + 2);
    QColor bubbleColor = row.option.palette.color(QPalette::Text);
    bubbleColor.setAlphaF(0.08);
    painter->setPen(Qt::NoPen);
    painter->setBrush(bubbleColor);
    painter->drawRoundedRect(bubble, bubble.height() / 2, bubble.height() / 2);
    painter->setPen(muted(row.option, row.onAccent));
    painter->drawText(bubble, Qt::AlignCenter, countText);
}

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
    tile.setAlphaF(row.onAccent ? 0.35 : 0.18);
    painter->setPen(Qt::NoPen);
    painter->setBrush(tile);
    painter->drawRoundedRect(badgeRect, 4, 4);
    QFont badgeFont = row.option.font;
    badgeFont.setWeight(QFont::Bold);
    badgeFont.setPointSizeF(row.option.font.pointSizeF() * 0.8);
    painter->setFont(badgeFont);
    painter->setPen(row.onAccent ? row.option.palette.color(QPalette::HighlightedText) : changeKindColor(kind));
    painter->drawText(badgeRect, Qt::AlignCenter, changeKindLetter(kind));
    return badgeRect.left();
}

QString countLabel(const QString &sign, int count)
{
    return count > 0 ? sign + QString::number(count) : QString();
}

// "+12 −3", ending at `right`. Returns where it starts.
int paintStats(const Row &row, int added, int removed, int right)
{
    QFont statsFont = row.option.font;
    statsFont.setPointSizeF(row.option.font.pointSizeF() * 0.86);
    statsFont.setWeight(QFont::DemiBold);
    const QFontMetrics statsMetrics(statsFont);
    const bool dark = isDark(row.option.palette);
    const QColor red = dark ? QColor("#f85149") : QColor("#cf222e");
    const QColor green = dark ? QColor("#3fb950") : QColor("#1a7f37");
    row.painter->setFont(statsFont);
    for (const auto &[text, color] : {std::pair{countLabel(QStringLiteral("−"), removed), red},
                                      std::pair{countLabel(QStringLiteral("+"), added), green}}) {
        if (text.isEmpty())
            continue;
        const int width = statsMetrics.horizontalAdvance(text);
        row.painter->setPen(row.onAccent ? row.option.palette.color(QPalette::HighlightedText) : color);
        row.painter->drawText(QRect(right - width, row.rect.top(), width, row.rect.height()),
                              Qt::AlignRight | Qt::AlignVCenter, text);
        right -= width + 6;
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
    painter->setPen(row.option.palette.color(row.onAccent ? QPalette::HighlightedText : QPalette::Text));
    const QString shownName = metrics.elidedText(name, Qt::ElideMiddle, available);
    painter->drawText(area, Qt::AlignLeft | Qt::AlignVCenter, shownName);
    const int nameWidth = metrics.horizontalAdvance(shownName) + 10;
    const bool folderFits = !folder.isEmpty() && available - nameWidth > 20;
    if (!folderFits)
        return;
    QFont small = row.option.font;
    small.setPointSizeF(row.option.font.pointSizeF() * 0.92);
    painter->setFont(small);
    painter->setPen(muted(row.option, row.onAccent));
    const QFontMetrics smallMetrics(small);
    painter->drawText(QRect(area.left() + nameWidth, area.top(), available - nameWidth, area.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      smallMetrics.elidedText(folder, Qt::ElideLeft, available - nameWidth));
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

void FileChangeDelegate::setSection(QTreeWidgetItem *item, const QString &title, int count)
{
    item->setText(0, title);
    item->setData(0, sectionCountRole, count);
}

QSize FileChangeDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const QFontMetrics metrics(option.font);
    const bool section = index.data(sectionCountRole).isValid();
    // Titles get room above them, separating the sections.
    const int height = qRound(metrics.height() * (section ? 2.3 : rowRatio));
    return {QStyledItemDelegate::sizeHint(option, index).width(), height};
}

void FileChangeDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem item = option;
    initStyleOption(&item, index);
    const QWidget *widget = option.widget;
    const QStyle *style = widget ? widget->style() : QApplication::style();
    const bool selected = option.state & QStyle::State_Selected;
    const bool onAccent = selected && (option.state & QStyle::State_Active);
    const Row row{painter, option, option.rect.adjusted(horizontalPadding, 0, -horizontalPadding, 0), onAccent};

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    if (index.data(sectionCountRole).isValid()) {
        // Selected, a title stands for all its files: the selection shows.
        if (selected) {
            QStyleOptionViewItem background = item;
            background.text.clear();
            style->drawControl(QStyle::CE_ItemViewItem, &background, painter, widget);
        }
        paintSectionTitle(row, item.text, index.data(sectionCountRole).toInt());
        painter->restore();
        return;
    }

    // Background, selection and hover from the style.
    item.text.clear();
    item.icon = QIcon();
    style->drawControl(QStyle::CE_ItemViewItem, &item, painter, widget);

    const QString path = index.data(pathRole).toString();
    const auto kind = static_cast<ChangeKind>(index.data(kindRole).toInt());
    const QString name = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
    const QString folder = folderLabel(path, kind, index.data(oldPathRole).toString());

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
