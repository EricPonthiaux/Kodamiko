// SPDX-License-Identifier: GPL-3.0-only

#include "TrailingLayout.h"

#include <QWidget>

TrailingLayout::TrailingLayout(QWidget *parent)
    : QLayout(parent)
{
}

TrailingLayout::~TrailingLayout()
{
    qDeleteAll(items_);
}

void TrailingLayout::addItem(QLayoutItem *item)
{
    items_.append(item);
    groups_.append(currentGroup_);
}

QLayoutItem *TrailingLayout::itemAt(int index) const
{
    return items_.value(index);
}

QLayoutItem *TrailingLayout::takeAt(int index)
{
    if (index < 0 || index >= items_.size())
        return nullptr;
    groups_.removeAt(index);
    return items_.takeAt(index);
}

int TrailingLayout::count() const
{
    return static_cast<int>(items_.size());
}

QSize TrailingLayout::sizeHint() const
{
    int width = 0;
    int height = 0;
    qsizetype previous = -1; // the last shown item
    for (qsizetype i = 0; i < items_.size(); ++i) {
        if (items_.at(i)->isEmpty())
            continue;
        const QSize hint = items_.at(i)->sizeHint();
        width += hint.width() + (previous >= 0 ? gapBetween(previous, i) : 0);
        height = qMax(height, hint.height());
        previous = i;
    }
    const QMargins margins = contentsMargins();
    return {width + margins.left() + margins.right(), height + margins.top() + margins.bottom()};
}

QSize TrailingLayout::minimumSize() const
{
    const QMargins margins = contentsMargins();
    const QSize extra(margins.left() + margins.right(), margins.top() + margins.bottom());
    for (const QLayoutItem *item : items_) {
        if (!item->isEmpty())
            return item->sizeHint() + extra;
    }
    return extra;
}

Qt::Orientations TrailingLayout::expandingDirections() const
{
    return {};
}

void TrailingLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    const QRect area = contentsRect();
    int right = area.right() + 1;
    // Once a widget has no room, those further left have none either: the
    // row keeps its order instead of filling the gap with a smaller one.
    bool full = false;
    qsizetype previous = -1; // the last shown item, at the right of this one
    for (qsizetype i = 0; i < items_.size(); ++i) {
        QLayoutItem *item = items_.at(i);
        if (item->isEmpty())
            continue;
        if (previous >= 0)
            right -= gapBetween(previous, i);
        previous = i;
        const QSize hint = item->sizeHint();
        const int height = qMin(hint.height(), area.height());
        const int top = area.top() + ((area.height() - height) / 2);
        const bool isAnchor = right == area.right() + 1;
        full = full || (!isAnchor && right - hint.width() < area.left());
        if (full) {
            // Out of the parent's rectangle, so clipped away, but keeping its
            // size: hiding it would fight the code that shows it.
            item->setGeometry(QRect(area.left() - hint.width(), top, hint.width(), height));
            continue;
        }
        right -= hint.width();
        item->setGeometry(QRect(right, top, hint.width(), height));
    }
}

int TrailingLayout::gapBetween(qsizetype left, qsizetype right) const
{
    return groups_.at(left) == groups_.at(right) ? gap() : qMax(0, groupSpacing_);
}

int TrailingLayout::gap() const
{
    return qMax(0, spacing());
}
