#include "CommitDelegate.h"

#include "CommitGraph.h"
#include "git/CommitLog.h"
#include "ui/AppFonts.h"

#include <QApplication>
#include <QHash>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QTreeWidgetItem>

#include <algorithm>
#include <array>
#include <tuple>

namespace {

constexpr int padding = 12;
constexpr int avatarSize = 20;
// The badges take at most this share of the first line: the summary first.
constexpr qreal maximumBadgesShare = 0.4;

// Item data roles of a commit row.
constexpr int authorRole = Qt::UserRole + 60;
constexpr int dateRole = Qt::UserRole + 61;
constexpr int shortIdRole = Qt::UserRole + 62;
constexpr int refsRole = Qt::UserRole + 63;
constexpr int graphRole = Qt::UserRole + 64;

constexpr int laneWidth = 14;
constexpr int dotRadius = 4;
// Wider graphs are cut at the right: the summary keeps its room.
constexpr int maximumShownLanes = 10;

// One color per line of history, in turn.
const std::array laneColors{QColor("#4c9be8"), QColor("#2fae8f"), QColor("#e07b53"), QColor("#c55bd8"),
                            QColor("#d6a33c"), QColor("#d65d8f"), QColor("#5a8f3c"), QColor("#7c6cf0")};

// Soft colors, one per author.
const std::array avatarColors{QColor("#7c6cf0"), QColor("#4c9be8"), QColor("#2fae8f"), QColor("#e07b53"),
                              QColor("#d65d8f"), QColor("#9a7d4f"), QColor("#5a8f3c"), QColor("#c55bd8")};

QColor dimmed(const QStyleOptionViewItem &option)
{
    QColor color = option.palette.color(QPalette::Text);
    color.setAlphaF(0.52);
    return color;
}

// Less saturated, to stay discreet next to the text.
QColor muted(const QColor &color)
{
    const QColor hsl = color.toHsl();
    return QColor::fromHslF(hsl.hslHueF(), hsl.hslSaturationF() * 0.55, hsl.lightnessF() * 0.9);
}

QColor laneColor(int color)
{
    return laneColors.at(size_t(color) % laneColors.size());
}

// Straight down, or curving from one lane to the other.
void drawSegment(QPainter *painter, QPointF from, QPointF to)
{
    QPainterPath path(from);
    if (qFuzzyCompare(from.x(), to.x())) {
        path.lineTo(to);
    } else {
        const qreal middle = (from.y() + to.y()) / 2;
        path.cubicTo(QPointF(from.x(), middle), QPointF(to.x(), middle), to);
    }
    painter->drawPath(path);
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
    item->setData(0, authorRole, commit.author);
    item->setData(0, dateRole, commit.date);
    item->setData(0, shortIdRole, commit.shortId);
    item->setData(0, refsRole, commit.refs);
    item->setToolTip(0, QStringLiteral("%1\n%2 <%3>\n%4")
                            .arg(commit.summary, commit.author, commit.email,
                                 QLocale().toString(commit.date.toLocalTime(), QLocale::LongFormat)));
}

void CommitDelegate::setGraphRow(QTreeWidgetItem *item, const GraphRow &row)
{
    item->setData(0, graphRole, QVariant::fromValue(row));
}

void CommitDelegate::setGraphLanes(int lanes)
{
    graphLanes_ = std::min(lanes, maximumShownLanes);
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
    // Claude in its own orange.
    const QColor color =
        muted(name.startsWith(QLatin1String("Claude")) ? QColor("#d97757")
                                                       : avatarColors.at(qHash(name) % avatarColors.size()));
    QPixmap pixmap(QSize(size, size) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(0, 0, size, size), size * 0.2, size * 0.2);
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
        return {"#c25b6b"};
    if (name.contains(QLatin1Char('/')))
        return {"#7d879a"}; // remote branch
    if (name.startsWith(QLatin1Char('v')) && name.size() > 1 && name.at(1).isDigit())
        return {"#b0813a"}; // version tag
    return {"#3f8f5a"};
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
    const QStyle *style = widget ? widget->style() : QApplication::style();
    // A selected row is slate blue (see ThemeStyle): the text keeps its colors.
    const QString summary = item.text;
    item.text.clear();
    style->drawControl(QStyle::CE_ItemViewItem, &item, painter, widget);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QRect rect = option.rect.adjusted(padding, 0, -padding, 0);
    if (const QVariant graph = index.data(graphRole); graphLanes_ > 0 && graph.isValid()) {
        const QRect column(rect.left() - padding / 2, option.rect.top(), graphLanes_ * laneWidth, option.rect.height());
        paintGraph(painter, option, graph.value<GraphRow>(), column);
        rect.setLeft(column.right() + padding / 2);
    }
    const QString author = index.data(authorRole).toString();
    const qreal ratio = painter->device()->devicePixelRatioF();
    const QRect avatarRect(rect.left(), rect.center().y() - avatarSize / 2, avatarSize, avatarSize);
    painter->drawPixmap(avatarRect, avatar(author, avatarSize, ratio));

    const QFontMetrics metrics(option.font);
    const int left = avatarRect.right() + 10;
    const int lineHeight = metrics.height();
    const QRect firstLine(left, rect.center().y() - lineHeight - 1, rect.right() - left, lineHeight);
    const QRect secondLine(left, rect.center().y() + 3, rect.right() - left, lineHeight);

    // First line: the badges at the right, then the summary in what is left.
    const int badgesLeft = paintBadges(painter, option, index.data(refsRole).toStringList(), firstLine);
    QFont summaryFont = option.font;
    summaryFont.setWeight(QFont::Medium);
    painter->setFont(summaryFont);
    painter->setPen(option.palette.color(QPalette::Text));
    const int available = badgesLeft - firstLine.left() - 8;
    painter->drawText(QRect(firstLine.left(), firstLine.top(), available, lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(summaryFont).elidedText(summary, Qt::ElideRight, available));

    // Second line: the ID at the right, author · date before it.
    const QFont idFont = AppFonts::codeFont(option.font.pointSizeF() * 0.88);
    const QString id = index.data(shortIdRole).toString();
    const int idWidth = QFontMetrics(idFont).horizontalAdvance(id);
    painter->setFont(idFont);
    painter->setPen(dimmed(option));
    painter->drawText(secondLine, Qt::AlignRight | Qt::AlignVCenter, id);
    QFont detailFont = option.font;
    detailFont.setPointSizeF(option.font.pointSizeF() * 0.92);
    painter->setFont(detailFont);
    const QString details = QStringLiteral("%1  ·  %2").arg(author, relativeDate(index.data(dateRole).toDateTime()));
    const int detailsWidth = secondLine.width() - idWidth - 12;
    painter->drawText(QRect(secondLine.left(), secondLine.top(), detailsWidth, secondLine.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(detailFont).elidedText(details, Qt::ElideRight, detailsWidth));
    painter->restore();
}

void CommitDelegate::paintGraph(QPainter *painter,
                                const QStyleOptionViewItem &option,
                                const GraphRow &row,
                                const QRect &rect)
{
    const auto x = [&rect](int lane) { return rect.left() + lane * laneWidth + laneWidth / 2.0; };
    const qreal middle = rect.top() + rect.height() / 2.0;
    painter->save();
    painter->setClipRect(rect);
    for (const auto &[half, top, bottom] : {std::tuple{&row.upper, qreal(rect.top()), middle},
                                            std::tuple{&row.lower, middle, qreal(rect.bottom() + 1)}}) {
        for (const GraphSegment &segment : *half) {
            painter->setPen(QPen(laneColor(segment.color), 2));
            drawSegment(painter, QPointF(x(segment.from), top), QPointF(x(segment.to), bottom));
        }
    }
    // The dot, ringed with the row's background to stand out from its lines.
    painter->setPen(QPen(option.palette.color(QPalette::Base), 2));
    painter->setBrush(laneColor(row.color));
    painter->drawEllipse(QPointF(x(row.lane), middle), dotRadius + 1, dotRadius + 1);
    painter->restore();
}

int CommitDelegate::paintBadges(QPainter *painter,
                                const QStyleOptionViewItem &option,
                                const QStringList &refs,
                                const QRect &line)
{
    QFont badgeFont = option.font;
    badgeFont.setPointSizeF(option.font.pointSizeF() * 0.82);
    badgeFont.setWeight(QFont::DemiBold);
    const QFontMetrics badgeMetrics(badgeFont);
    const int leftmost = line.right() - int(line.width() * maximumBadgesShare);
    int right = line.right() + 1;
    painter->setFont(badgeFont);
    for (qsizetype i = 0; i < refs.size(); ++i) {
        const QString &name = refs.at(i);
        const int width = badgeMetrics.horizontalAdvance(name) + 10;
        // No room left: how many more, instead.
        if (right - width < leftmost) {
            const QString more = QStringLiteral("+%1").arg(refs.size() - i);
            const int moreWidth = badgeMetrics.horizontalAdvance(more);
            painter->setPen(dimmed(option));
            painter->drawText(QRect(right - moreWidth, line.top(), moreWidth, line.height()),
                              Qt::AlignRight | Qt::AlignVCenter, more);
            return right - moreWidth;
        }
        const QRectF badge(right - width, line.top() + (line.height() - badgeMetrics.height() - 2) / 2.0, width,
                           badgeMetrics.height() + 2);
        QColor fill = refColor(name);
        fill.setAlphaF(0.2);
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        painter->drawRoundedRect(badge, 2, 2);
        painter->setPen(refColor(name).lighter(130));
        painter->drawText(badge, Qt::AlignCenter, name);
        right -= width + 4;
    }
    return right;
}
