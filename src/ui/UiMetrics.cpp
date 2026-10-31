// SPDX-License-Identifier: GPL-3.0-only

#include "UiMetrics.h"

#include "AppFonts.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QFontInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QTabBar>
#include <QWidget>

#include <algorithm>

namespace UiMetrics {

int scaled(int referencePx)
{
    // The size asked for, not the matched font's (11 pt matches 11.25), in
    // the points of Preferences, which are the same pixels on every system.
    const QFont font = QGuiApplication::font();
    const qreal fontSize = font.pointSizeF() > 0 ? font.pointSizeF() : QFontInfo(font).pointSizeF();
    return qRound(referencePx * AppFonts::kdkPoints(fontSize) / AppFonts::referenceInterfaceSize);
}

int tabHeight()
{
    constexpr int paddingsPx = 2 * 7;
    constexpr int accentLinePx = 2;
    return QFontMetrics(QGuiApplication::font()).height() + paddingsPx + accentLinePx;
}

int tabBarHeight(const QTabBar &tabs)
{
    if (tabs.count() > 0)
        return tabs.sizeHint().height();
    // Beside the tabs, so that the style sheet's rules for them apply.
    QTabBar probe(tabs.parentWidget());
    probe.setFont(tabs.font());
    probe.setDocumentMode(tabs.documentMode());
    probe.setTabsClosable(tabs.tabsClosable());
    probe.addTab(QStringLiteral("M"));
    probe.ensurePolished();
    return probe.sizeHint().height();
}

int controlHeight()
{
    return std::min(scaled(toolbarButtonSize), tabHeight());
}

void setControlHeight(QWidget *widget)
{
    AppFonts::followApplicationFont(widget, [widget] { widget->setFixedHeight(controlHeight()); });
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
