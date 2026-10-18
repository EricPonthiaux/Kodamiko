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

} // namespace

FileChangeDelegate::FileChangeDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void FileChangeDelegate::setChange(QTreeWidgetItem *item, const FileChange &change)
{
    item->setText(0, changeLabel(change));
    item->setData(0, PathRole, change.path);
    item->setData(0, KindRole, static_cast<int>(change.kind));
    item->setData(0, OldPathRole, change.oldPath);
    item->setToolTip(0, changeLabel(change) + QLatin1Char('\n') + changeStatusText(change));
}

void FileChangeDelegate::setStats(QTreeWidgetItem *item, const FilePatch &patch)
{
    item->setData(0, AddedRole, patch.addedLines());
    item->setData(0, RemovedRole, patch.removedLines());
}

void FileChangeDelegate::setSection(QTreeWidgetItem *item, const QString &title, int count)
{
    item->setText(0, title);
    item->setData(0, SectionCountRole, count);
}

QSize FileChangeDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const QFontMetrics metrics(option.font);
    const bool section = index.data(SectionCountRole).isValid();
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
    // Selected in the active window, the row is on the accent: white text.
    // Otherwise its background is a soft gray and the text keeps its colors.
    const bool onAccent = selected && (option.state & QStyle::State_Active);
    const QRect rect = option.rect.adjusted(horizontalPadding, 0, -horizontalPadding, 0);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    if (index.data(SectionCountRole).isValid()) {
        // Selected, a title stands for all its files: the selection shows.
        if (selected) {
            QStyleOptionViewItem background = item;
            background.text.clear();
            style->drawControl(QStyle::CE_ItemViewItem, &background, painter, widget);
        }
        // "STAGED  2": small capitals, dimmed, on the row's lower part.
        QFont font = option.font;
        font.setCapitalization(QFont::AllUppercase);
        font.setPointSizeF(font.pointSizeF() * 0.82);
        font.setWeight(QFont::DemiBold);
        font.setLetterSpacing(QFont::PercentageSpacing, 108);
        painter->setFont(font);
        painter->setPen(muted(option, onAccent));
        const QFontMetrics metrics(font);
        const QRect text(rect.left(), rect.bottom() - metrics.height() - 4, rect.width(), metrics.height());
        const QString title = item.text;
        painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter, title);
        const QString count = QString::number(index.data(SectionCountRole).toInt());
        const int x = rect.left() + metrics.horizontalAdvance(title) + 8;
        const QRectF bubble(x, text.center().y() - metrics.height() / 2.0 - 1, metrics.horizontalAdvance(count) + 12,
                            metrics.height() + 2);
        QColor bubbleColor = option.palette.color(QPalette::Text);
        bubbleColor.setAlphaF(0.08);
        painter->setPen(Qt::NoPen);
        painter->setBrush(bubbleColor);
        painter->drawRoundedRect(bubble, bubble.height() / 2, bubble.height() / 2);
        painter->setPen(muted(option, onAccent));
        painter->drawText(bubble, Qt::AlignCenter, count);
        painter->restore();
        return;
    }

    // Background, selection and hover from the style.
    item.text.clear();
    item.icon = QIcon();
    style->drawControl(QStyle::CE_ItemViewItem, &item, painter, widget);

    const QString path = index.data(PathRole).toString();
    const auto kind = static_cast<ChangeKind>(index.data(KindRole).toInt());
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    const QString name = path.mid(slash + 1);
    QString folder = slash >= 0 ? path.left(slash) : QString();
    if (kind == ChangeKind::Renamed)
        folder = folder.isEmpty() ? QStringLiteral("← %1").arg(index.data(OldPathRole).toString())
                                  : folder + QStringLiteral("  ← %1").arg(index.data(OldPathRole).toString());

    const QFontMetrics metrics(option.font);
    const int iconSize = 16;
    int x = rect.left();
    FileIcons::file(name, isDark(option.palette))
        .paint(painter, QRect(x, rect.center().y() - iconSize / 2, iconSize, iconSize));
    x += iconSize + 8;

    // The badge at the right: a soft tile of the change's color.
    const QString letter = changeKindLetter(kind);
    const int badge = metrics.height();
    const QRectF badgeRect(rect.right() - badge, rect.center().y() - badge / 2.0, badge, badge);
    QColor tile = changeKindColor(kind);
    tile.setAlphaF(onAccent ? 0.35 : 0.18);
    painter->setPen(Qt::NoPen);
    painter->setBrush(tile);
    painter->drawRoundedRect(badgeRect, 4, 4);
    QFont badgeFont = option.font;
    badgeFont.setWeight(QFont::Bold);
    badgeFont.setPointSizeF(option.font.pointSizeF() * 0.8);
    painter->setFont(badgeFont);
    painter->setPen(onAccent ? option.palette.color(QPalette::HighlightedText) : changeKindColor(kind));
    painter->drawText(badgeRect, Qt::AlignCenter, letter);

    // "+12 −3" left of the badge, when known.
    int right = int(badgeRect.left()) - 10;
    if (const QVariant added = index.data(AddedRole); added.isValid()) {
        QFont statsFont = option.font;
        statsFont.setPointSizeF(option.font.pointSizeF() * 0.86);
        statsFont.setWeight(QFont::DemiBold);
        const QFontMetrics statsMetrics(statsFont);
        const QString plus = added.toInt() > 0 ? QStringLiteral("+%1").arg(added.toInt()) : QString();
        const int removed = index.data(RemovedRole).toInt();
        const QString minus = removed > 0 ? QStringLiteral("−%1").arg(removed) : QString();
        const bool dark = isDark(option.palette);
        painter->setFont(statsFont);
        for (const auto &[text, color] : {std::pair{minus, dark ? QColor("#f85149") : QColor("#cf222e")},
                                          std::pair{plus, dark ? QColor("#3fb950") : QColor("#1a7f37")}}) {
            if (text.isEmpty())
                continue;
            const int width = statsMetrics.horizontalAdvance(text);
            painter->setPen(onAccent ? option.palette.color(QPalette::HighlightedText) : color);
            painter->drawText(QRect(right - width, rect.top(), width, rect.height()), Qt::AlignRight | Qt::AlignVCenter,
                              text);
            right -= width + 6;
        }
        right -= 4;
    }

    // Name, then the folder, dimmed, cut short if needed.
    const int available = right - x;
    painter->setFont(option.font);
    painter->setPen(option.palette.color(onAccent ? QPalette::HighlightedText : QPalette::Text));
    const QString shownName = metrics.elidedText(name, Qt::ElideMiddle, available);
    painter->drawText(QRect(x, rect.top(), available, rect.height()), Qt::AlignLeft | Qt::AlignVCenter, shownName);
    const int nameWidth = metrics.horizontalAdvance(shownName) + 10;
    if (!folder.isEmpty() && available - nameWidth > 20) {
        QFont small = option.font;
        small.setPointSizeF(option.font.pointSizeF() * 0.92);
        painter->setFont(small);
        painter->setPen(muted(option, onAccent));
        const QFontMetrics smallMetrics(small);
        painter->drawText(QRect(x + nameWidth, rect.top(), available - nameWidth, rect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          smallMetrics.elidedText(folder, Qt::ElideLeft, available - nameWidth));
    }
    painter->restore();
}
