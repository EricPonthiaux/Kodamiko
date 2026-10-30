// SPDX-License-Identifier: GPL-3.0-only

#include "MergeOverview.h"

#include "ui/UiMetrics.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

namespace {

constexpr int widthPx = 14;
constexpr int markInsetPx = 3;
// A one-row conflict in a long file stays visible.
constexpr qreal minimumMarkPx = 4;

} // namespace

MergeOverview::MergeOverview(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , theme_(theme)
{
    setFixedWidth(UiMetrics::scaled(widthPx));
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("The whole file: a mark per conflict"));
}

void MergeOverview::setMap(const Map &map)
{
    map_ = map;
    update();
}

void MergeOverview::applyTheme(const Theme &theme)
{
    theme_ = theme;
    update();
}

QSize MergeOverview::sizeHint() const
{
    return {UiMetrics::scaled(widthPx), 0};
}

qreal MergeOverview::yOf(int row) const
{
    return map_.totalRows > 0 ? qreal(row) * height() / map_.totalRows : 0;
}

QRectF MergeOverview::markRect(const Conflict &conflict) const
{
    const qreal top = yOf(conflict.firstRow);
    const qreal height = std::max(minimumMarkPx, yOf(conflict.firstRow + conflict.rows) - top);
    const int inset = UiMetrics::scaled(markInsetPx);
    return {qreal(inset), top, qreal(width() - 2 * inset), height};
}

void MergeOverview::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), theme_.window);
    painter.setPen(theme_.line);
    painter.drawLine(0, 0, 0, height());
    painter.setRenderHint(QPainter::Antialiasing);
    // The rows in view, under the marks.
    const QRectF view(1.5, yOf(map_.firstVisibleRow) + 0.5, width() - 2.5,
                      std::max(4.0, yOf(map_.firstVisibleRow + map_.visibleRows) - yOf(map_.firstVisibleRow)) - 1);
    QColor viewFill = theme_.foreground;
    viewFill.setAlphaF(0.07);
    painter.setPen(QPen(theme_.mutedText, 1));
    painter.setBrush(viewFill);
    painter.drawRoundedRect(view, 2, 2);
    for (int i = 0; i < map_.conflicts.size(); ++i) {
        const Conflict &conflict = map_.conflicts[i];
        QRectF mark = markRect(conflict);
        const bool isCurrent = i == map_.current;
        if (isCurrent)
            mark.adjust(-markInsetPx + 1, 0, markInsetPx - 1, 0);
        painter.setPen(isCurrent ? QPen(theme_.foreground, 1.5) : Qt::NoPen);
        painter.setBrush(ConflictStates::color(conflict.state, theme_));
        painter.drawRoundedRect(mark, 2, 2);
    }
}

void MergeOverview::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || map_.totalRows <= 0)
        return;
    const QPointF position = event->position();
    for (int i = 0; i < map_.conflicts.size(); ++i) {
        if (markRect(map_.conflicts[i]).adjusted(0, -2, 0, 2).contains(QPointF(width() / 2.0, position.y()))) {
            emit conflictRequested(i);
            return;
        }
    }
    emit rowRequested(int(position.y() * map_.totalRows / std::max(1, height())));
}
