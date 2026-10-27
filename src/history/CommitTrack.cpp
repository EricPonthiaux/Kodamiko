// SPDX-License-Identifier: GPL-3.0-only

#include "CommitTrack.h"

#include "CommitDelegate.h"

#include <QHelpEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace {

constexpr int sideMarginPx = 28;
constexpr int barTopPx = 10;
constexpr int barHeightPx = 34;
constexpr int barWidthPx = 8;
constexpr int axisYPx = barTopPx + barHeightPx + 8;
constexpr int dotRadiusPx = 5;
constexpr int dayGapPx = 10;
constexpr int heightPx = axisYPx + 26;
// A click this near a dot chooses it.
constexpr qreal reachPx = 10;

} // namespace

CommitTrack::CommitTrack(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
}

void CommitTrack::setMarks(QList<Mark> marks)
{
    std::ranges::sort(marks, {}, [](const Mark &mark) { return mark.commit.date; });
    marks_ = std::move(marks);
    update();
}

void CommitTrack::setChosen(const QString &id)
{
    chosen_ = id;
    update();
}

void CommitTrack::applyTheme(const Theme &theme)
{
    theme_ = theme;
    update();
}

QSize CommitTrack::sizeHint() const
{
    return {400, heightPx};
}

bool CommitTrack::event(QEvent *event)
{
    if (event->type() != QEvent::ToolTip)
        return QWidget::event(event);
    const auto *help = static_cast<QHelpEvent *>(event);
    const Mark *mark = markAt(help->pos());
    if (!mark) {
        QToolTip::hideText();
        return true;
    }
    const QString date = QLocale().toString(mark->commit.date.toLocalTime(), QLocale::ShortFormat);
    QToolTip::showText(help->globalPos(),
                       tr("%1\n%2 · %3 · %4\n%n line(s) of the file", nullptr, mark->lineCount)
                           .arg(mark->commit.summary, mark->commit.author, date, mark->commit.shortId),
                       this);
    return true;
}

void CommitTrack::mousePressEvent(QMouseEvent *event)
{
    if (const Mark *mark = markAt(event->position()))
        emit commitClicked(mark->commit.id);
}

const CommitTrack::Mark *CommitTrack::markAt(QPointF position) const
{
    const Mark *nearest = nullptr;
    qreal nearestDistance = reachPx;
    for (const Mark &mark : marks_) {
        const qreal distance = std::abs(xOf(mark.commit.date) - position.x());
        if (distance <= nearestDistance) {
            nearest = &mark;
            nearestDistance = distance;
        }
    }
    return nearest;
}

qreal CommitTrack::xOf(const QDateTime &date) const
{
    const qreal left = sideMarginPx;
    const qreal width = std::max(1, this->width() - 2 * sideMarginPx);
    if (marks_.size() < 2)
        return left + width / 2;
    const qint64 first = marks_.first().commit.date.toSecsSinceEpoch();
    const qint64 span = std::max<qint64>(1, marks_.last().commit.date.toSecsSinceEpoch() - first);
    return left + width * qreal(date.toSecsSinceEpoch() - first) / qreal(span);
}

void CommitTrack::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(theme_.border, 1));
    painter.drawLine(QPointF(sideMarginPx / 2.0, axisYPx + 0.5), QPointF(width() - sideMarginPx / 2.0, axisYPx + 0.5));
    const int highestCount =
        marks_.isEmpty() ? 1 : std::ranges::max(marks_, {}, [](const Mark &mark) { return mark.lineCount; }).lineCount;
    for (const Mark &mark : marks_)
        paintMark(painter, mark, std::max(1, highestCount));
    paintDays(painter);
}

void CommitTrack::paintMark(QPainter &painter, const Mark &mark, int highestCount) const
{
    const qreal x = xOf(mark.commit.date);
    // By area rather than height: a few lines still show.
    const qreal barHeight = std::max(2.0, barHeightPx * std::sqrt(qreal(mark.lineCount) / highestCount));
    painter.setPen(Qt::NoPen);
    painter.setBrush(mark.ageColor);
    painter.drawRoundedRect(QRectF(x - barWidthPx / 2.0, barTopPx + barHeightPx - barHeight, barWidthPx, barHeight), 2,
                            2);
    const bool isChosen = mark.commit.id == chosen_;
    const QColor ring = mark.isMadeWithClaude ? CommitDelegate::claudeColor() : theme_.accent;
    painter.setPen(QPen(ring, 2));
    painter.setBrush(isChosen ? ring : theme_.window);
    painter.drawEllipse(QPointF(x, axisYPx + 0.5), dotRadiusPx, dotRadiusPx);
}

void CommitTrack::paintDays(QPainter &painter) const
{
    QFont font = this->font();
    font.setPointSizeF(font.pointSizeF() * 0.85);
    painter.setFont(font);
    painter.setPen(theme_.mutedText);
    const QFontMetrics metrics(font);
    qreal lastRight = -dayGapPx;
    for (const Mark &mark : marks_) {
        const QString day = QLocale().toString(mark.commit.date.toLocalTime().date(), QStringLiteral("MMM d"));
        const qreal width = metrics.horizontalAdvance(day);
        const qreal left = std::clamp(xOf(mark.commit.date) - width / 2, 0.0, this->width() - width);
        if (left < lastRight + dayGapPx)
            continue;
        painter.drawText(QPointF(left, axisYPx + dotRadiusPx + 4 + metrics.ascent()), day);
        lastRight = left + width;
    }
}
