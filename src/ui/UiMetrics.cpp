// SPDX-License-Identifier: GPL-3.0-only

#include "UiMetrics.h"

#include "AppFonts.h"

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

} // namespace UiMetrics
