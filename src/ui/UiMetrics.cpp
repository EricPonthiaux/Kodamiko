// SPDX-License-Identifier: GPL-3.0-only

#include "UiMetrics.h"

#include "AppFonts.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QFontInfo>
#include <QGuiApplication>
#include <QWidget>

namespace UiMetrics {
int scaled(int referencePx)
{
    // The size asked for, not the matched font's (11 pt matches 11.25).
    const QFont font = QGuiApplication::font();
    const qreal fontSize = font.pointSizeF() > 0 ? font.pointSizeF() : QFontInfo(font).pointSizeF();
    return qRound(referencePx * fontSize / AppFonts::defaultInterfaceSize);
}

void setScaledHeight(QWidget *widget, int referencePx)
{
    AppFonts::followApplicationFont(widget, [widget, referencePx] { widget->setFixedHeight(scaled(referencePx)); });
}

QSize scaledSquare(int referencePx)
{
    const int side = scaled(referencePx);
    return {side, side};
}

void setScaledSize(QWidget *widget, QSize referencePx)
{
    AppFonts::followApplicationFont(widget, [widget, referencePx] {
        widget->setFixedSize(scaled(referencePx.width()), scaled(referencePx.height()));
    });
}

void setScaledIconSize(QAbstractButton *button, int referencePx)
{
    AppFonts::followApplicationFont(button, [button, referencePx] { button->setIconSize(scaledSquare(referencePx)); });
}

void setScaledIconSize(QAbstractItemView *view, int referencePx)
{
    AppFonts::followApplicationFont(view, [view, referencePx] { view->setIconSize(scaledSquare(referencePx)); });
}

} // namespace UiMetrics
