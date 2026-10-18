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
}

QLayoutItem *TrailingLayout::itemAt(int index) const
{
    return items_.value(index);
}

QLayoutItem *TrailingLayout::takeAt(int index)
{
    if (index < 0 || index >= items_.size())
        return nullptr;
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
    int shown = 0;
    for (const QLayoutItem *item : items_) {
        if (item->isEmpty())
            continue;
        const QSize hint = item->sizeHint();
        width += hint.width();
        height = qMax(height, hint.height());
        ++shown;
    }
    width += qMax(0, shown - 1) * gap();
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
    for (QLayoutItem *item : items_) {
        if (item->isEmpty())
            continue;
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
        right -= gap();
    }
}

int TrailingLayout::gap() const
{
    return qMax(0, spacing());
}
