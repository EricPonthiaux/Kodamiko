// SPDX-License-Identifier: GPL-3.0-only

#include "PopupRows.h"

#include "PopupMetrics.h"
#include "Theme.h"
#include "ThemeStyle.h"

#include <QPainter>

namespace PopupRows {

void paintBackground(QPainter *painter, const QRectF &row, QStyle::State state, const Theme &theme)
{
    QColor fill;
    if (state & QStyle::State_Selected)
        fill = ThemeStyle::selectionColor(theme);
    else if (state & QStyle::State_MouseOver)
        fill = theme.hover;
    else
        return;
    painter->setPen(Qt::NoPen);
    painter->setBrush(fill);
    painter->drawRoundedRect(row, PopupMetrics::rowRadius, PopupMetrics::rowRadius);
}

} // namespace PopupRows
