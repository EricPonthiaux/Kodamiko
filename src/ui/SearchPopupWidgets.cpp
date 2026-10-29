// SPDX-License-Identifier: GPL-3.0-only

#include "SearchPopupWidgets.h"

#include "AppFonts.h"
#include "PopupWidgets.h"
#include "ThemeStyle.h"
#include "TreeMetrics.h"
#include "UiMetrics.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QStyle>

#include <algorithm>

namespace SearchPopup {

QString styleSheet(const Theme &theme)
{
    return popupStyleSheet(theme, QStringLiteral(R"(
QFrame#searchArea { background: transparent; border: none; border-bottom: 1px solid @line; }
QLineEdit#popupSearch, QLineEdit#popupSearch:focus {
    background: transparent; border: none; border-radius: 0; padding: 8px 4px;
}
QTreeWidget#popupList { padding: 3px 0; }
QTreeWidget#popupList::item { padding: 0 10px; border-radius: 0; }
QLabel#searchSummary { color: @muted; }
QLabel#searchEmpty { color: @muted; padding: 12px; }
QLabel#searchHint { color: @muted; }
QTabBar#goToTabs::tab { background: transparent; color: @muted; border: none; border-bottom: 2px solid transparent; border-radius: 0; padding: 5px 10px; margin: 0; }
QTabBar#goToTabs::tab:hover { background: @hover; color: @text; }
QTabBar#goToTabs::tab:selected { background: transparent; color: @text; border-bottom-color: @accent; }
QToolButton#searchOption { background: transparent; border: 1px solid transparent; border-radius: 3px; padding: 2px; }
QToolButton#searchOption:hover { background: @hover; }
QToolButton#searchOption:checked { background: @slate; color: @accentText; border-color: @accent; }
QFrame#searchFilters { background: transparent; border: none; border-bottom: 1px solid @line; }
QLineEdit#fileMask { border-radius: 2px; padding: 3px 6px; }
QFrame#searchPreviewHeader { background: transparent; border: none; border-top: 1px solid @line; border-bottom: 1px solid @line; }
QLabel#searchPreviewPath { color: @muted; }
)"));
}

QWidget *searchArea(QLineEdit *field, QWidget *parent)
{
    auto *area = new QFrame(parent);
    area->setObjectName(QStringLiteral("searchArea"));
    auto *layout = new QHBoxLayout(area);
    layout->setContentsMargins(6, 0, 8, 0);
    layout->setSpacing(2);
    layout->addWidget(field, 1);
    return area;
}

QWidget *footer(const QList<QPair<QString, QString>> &hints, QLabel *summary, QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("popupFooter"));
    auto *layout = new QHBoxLayout(frame);
    layout->setContentsMargins(10, 5, 10, 5);
    layout->setSpacing(12);
    for (const auto &[key, action] : hints) {
        auto *label = new QLabel(key + QLatin1Char(' ') + action, frame);
        label->setObjectName(QStringLiteral("searchHint"));
        label->setTextFormat(Qt::PlainText);
        AppFonts::setInterfaceFont(label, 0.85, QFont::Normal);
        layout->addWidget(label);
    }
    layout->addStretch();
    layout->addWidget(summary);
    return frame;
}

QLabel *emptyState(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QStringLiteral("searchEmpty"));
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}

QLabel *resultSummary(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QStringLiteral("searchSummary"));
    label->setTextFormat(Qt::PlainText);
    AppFonts::setInterfaceFont(label, 0.85, QFont::Normal);
    return label;
}

QSize ResultDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(std::max(size.height(), treeRowHeight(option.font)));
    return size;
}

void ResultDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem item = option;
    initStyleOption(&item, index);
    const QString title = item.text;
    item.text.clear();
    const QStyle *style = item.widget->style();
    style->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);
    QRect rect = style->subElementRect(QStyle::SE_ItemViewItemText, &item, item.widget).adjusted(2, 0, 0, 0);
    // Some styles return only the title's natural width; context uses the rest of the column.
    rect.setRight(item.rect.right() - 10);
    const QString detail = index.data(detailRole).toString();
    QFont detailFont = item.font;
    detailFont.setPointSizeF(detailFont.pointSizeF() * 0.9);
    const QFontMetrics titleMetrics(item.font);
    const QFontMetrics detailMetrics(detailFont);
    const int gap = UiMetrics::scaled(12);
    // Long names keep priority, while leaving enough context to distinguish duplicates.
    const int titleLimit = detail.isEmpty() ? rect.width() : rect.width() * 2 / 3;
    const int titleWidth = std::min(titleMetrics.horizontalAdvance(title), titleLimit);
    painter->save();
    painter->setClipRect(rect);
    painter->setFont(item.font);
    painter->setPen(item.palette.color(QPalette::Text));
    painter->drawText(QRect(rect.left(), rect.top(), titleLimit, rect.height()), Qt::AlignVCenter,
                      titleMetrics.elidedText(title, Qt::ElideRight, titleLimit));
    const QRect detailRect = rect.adjusted(titleWidth + gap, 0, 0, 0);
    painter->setFont(detailFont);
    painter->setPen(ThemeStyle::applied().mutedText);
    painter->drawText(detailRect, Qt::AlignVCenter,
                      detailMetrics.elidedText(detail, Qt::ElideMiddle, detailRect.width()));
    painter->restore();
}

} // namespace SearchPopup
