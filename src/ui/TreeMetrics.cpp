#include "TreeMetrics.h"

#include <QFontMetrics>
#include <QStyledItemDelegate>
#include <QTreeView>

#include <algorithm>
#include <cmath>
#include <typeinfo>

namespace {

constexpr qreal rowRatio = 1.6;
constexpr qreal indentRatio = 1.25;

// Enforced row height; the rest of the painting is the standard delegate's.
class RowHeightDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), treeRowHeight(option.font)));
        return size;
    }
};

} // namespace

void applyTreeMetrics(QTreeView *tree, const QFont &font)
{
    tree->setFont(font);

    const int textHeight = QFontMetrics(font).height();
    tree->setIconSize(QSize(textHeight, textHeight));
    tree->setIndentation(qRound(textHeight * indentRatio));

    // Only the standard delegate is replaced: a custom one (file tree,
    // changes, history…) paints and sizes its rows itself, and code may
    // rely on its type.
    const QAbstractItemDelegate *previous = tree->itemDelegate();
    // The exact type: subclasses without Q_OBJECT share the base's metaObject.
    if (previous && typeid(*previous) != typeid(QStyledItemDelegate))
        return;
    tree->setItemDelegate(new RowHeightDelegate(tree));
    if (previous && previous->parent() == tree)
        delete previous;
}

int treeRowHeight(const QFont &font)
{
    return qRound(QFontMetrics(font).height() * rowRatio);
}
