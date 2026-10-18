#include "CommitDelegate.h"

#include "git/CommitLog.h"

#include <QApplication>
#include <QHash>
#include <QLocale>
#include <QPainter>
#include <QTreeWidgetItem>

namespace {

constexpr int padding = 12;
constexpr int avatarSize = 28;

QColor dimmed(const QStyleOptionViewItem &option, bool selected)
{
    QColor color = option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    color.setAlphaF(selected ? 0.75 : 0.52);
    return color;
}

QString initials(const QString &name)
{
    const QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty())
        return QStringLiteral("?");
    QString result = words.first().left(1);
    if (words.size() > 1)
        result += words.last().left(1);
    return result.toUpper();
}

} // namespace

CommitDelegate::CommitDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void CommitDelegate::setCommit(QTreeWidgetItem *item, const CommitSummary &commit)
{
    item->setText(0, commit.summary);
    item->setData(0, AuthorRole, commit.author);
    item->setData(0, DateRole, commit.date);
    item->setData(0, ShortIdRole, commit.shortId);
    item->setData(0, RefsRole, commit.refs);
    item->setToolTip(0, QStringLiteral("%1\n%2 <%3>\n%4")
                            .arg(commit.summary, commit.author, commit.email,
                                 QLocale().toString(commit.date.toLocalTime(), QLocale::LongFormat)));
}

QString CommitDelegate::relativeDate(const QDateTime &date, const QDateTime &now)
{
    const qint64 seconds = date.secsTo(now);
    if (seconds < 60)
        return tr("just now");
    if (seconds < 3600) {
        const int minutes = int(seconds / 60);
        return minutes == 1 ? tr("1 minute ago") : tr("%1 minutes ago").arg(minutes);
    }
    if (seconds < 86400) {
        const int hours = int(seconds / 3600);
        return hours == 1 ? tr("1 hour ago") : tr("%1 hours ago").arg(hours);
    }
    const qint64 days = date.toLocalTime().date().daysTo(now.toLocalTime().date());
    if (days == 1)
        return tr("yesterday");
    if (days < 7)
        return tr("%1 days ago").arg(days);
    return QLocale().toString(date.toLocalTime().date(), QLocale::ShortFormat);
}

// Painted for every visible row at every repaint (hovering repaints): each
// avatar is drawn once.
QPixmap CommitDelegate::avatar(const QString &name, int size, qreal ratio)
{
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1\n%2\n%3").arg(name).arg(size).arg(ratio);
    if (const auto it = cache.constFind(key); it != cache.constEnd())
        return it.value();
    if (cache.size() > 1000)
        cache.clear();
    return cache.insert(key, drawAvatar(name, size, ratio)).value();
}

QPixmap CommitDelegate::drawAvatar(const QString &name, int size, qreal ratio)
{
    // Soft colors, one per author.
    static const QColor colors[] = {QColor("#7c6cf0"), QColor("#4c9be8"), QColor("#2fae8f"), QColor("#e07b53"),
                                    QColor("#d65d8f"), QColor("#9a7d4f"), QColor("#5a8f3c"), QColor("#c55bd8")};
    // Claude in its own orange.
    const QColor color = name.startsWith(QLatin1String("Claude"))
                             ? QColor("#d97757")
                             : colors[qHash(name) % (sizeof(colors) / sizeof(colors[0]))];
    QPixmap pixmap(QSize(size, size) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QRectF(0, 0, size, size));
    QFont font = QApplication::font();
    font.setPixelSize(qRound(size * 0.4));
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, initials(name));
    return pixmap;
}

QColor CommitDelegate::refColor(const QString &name)
{
    if (name == QLatin1String("HEAD"))
        return QColor("#c25b6b");
    if (name.contains(QLatin1Char('/')))
        return QColor("#7d879a"); // remote branch
    if (name.startsWith(QLatin1Char('v')) && name.size() > 1 && name.at(1).isDigit())
        return QColor("#b0813a"); // version tag
    return QColor("#3f8f5a");
}

QSize CommitDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const QFontMetrics metrics(option.font);
    return {QStyledItemDelegate::sizeHint(option, index).width(), metrics.height() * 2 + 18};
}

void CommitDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem item = option;
    initStyleOption(&item, index);
    const QWidget *widget = option.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    // Selected in the active window, the row is on the accent: white text.
    // Otherwise its background is a soft gray and the text keeps its colors.
    const bool selected = (option.state & QStyle::State_Selected) && (option.state & QStyle::State_Active);
    const QString summary = item.text;
    item.text.clear();
    style->drawControl(QStyle::CE_ItemViewItem, &item, painter, widget);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRect rect = option.rect.adjusted(padding, 0, -padding, 0);
    const QString author = index.data(AuthorRole).toString();
    const qreal ratio = painter->device()->devicePixelRatioF();
    const QRect avatarRect(rect.left(), rect.center().y() - avatarSize / 2, avatarSize, avatarSize);
    painter->drawPixmap(avatarRect, avatar(author, avatarSize, ratio));

    const QFontMetrics metrics(option.font);
    const int left = avatarRect.right() + 12;
    const int lineHeight = metrics.height();
    const int top = rect.center().y() - lineHeight - 1;
    int x = left;

    // First line: pills, then the summary.
    QFont pillFont = option.font;
    pillFont.setPointSizeF(option.font.pointSizeF() * 0.82);
    pillFont.setWeight(QFont::DemiBold);
    const QFontMetrics pillMetrics(pillFont);
    for (const QString &name : index.data(RefsRole).toStringList()) {
        const int width = pillMetrics.horizontalAdvance(name) + 12;
        const QRectF pill(x, top + (lineHeight - pillMetrics.height() - 2) / 2.0, width, pillMetrics.height() + 2);
        QColor fill = refColor(name);
        fill.setAlphaF(0.22);
        painter->setPen(QPen(refColor(name), 1));
        painter->setBrush(fill);
        painter->drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
        painter->setFont(pillFont);
        painter->setPen(selected ? option.palette.color(QPalette::HighlightedText) : refColor(name).lighter(130));
        painter->drawText(pill, Qt::AlignCenter, name);
        x += width + 6;
    }
    QFont summaryFont = option.font;
    summaryFont.setWeight(QFont::Medium);
    painter->setFont(summaryFont);
    painter->setPen(option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
    const int available = rect.right() - x;
    painter->drawText(QRect(x, top, available, lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(summaryFont).elidedText(summary, Qt::ElideRight, available));

    // Second line: author · date · ID.
    QFont detailFont = option.font;
    detailFont.setPointSizeF(option.font.pointSizeF() * 0.92);
    painter->setFont(detailFont);
    painter->setPen(dimmed(option, selected));
    const QString details =
        QStringLiteral("%1  ·  %2  ·  %3")
            .arg(author, relativeDate(index.data(DateRole).toDateTime()), index.data(ShortIdRole).toString());
    const QFontMetrics detailMetrics(detailFont);
    painter->drawText(QRect(left, rect.center().y() + 3, rect.right() - left, detailMetrics.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      detailMetrics.elidedText(details, Qt::ElideRight, rect.right() - left));
    painter->restore();
}
