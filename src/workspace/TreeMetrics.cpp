#include "TreeMetrics.h"

#include <QApplication>
#include <QFontMetrics>
#include <QStyledItemDelegate>
#include <QTreeView>

#include <algorithm>
#include <typeinfo>
#include <cmath>

namespace {

constexpr qreal rowRatio = 1.6;
constexpr qreal indentRatio = 1.25;

// Enforced row height; the rest of the painting is the standard delegate's.
class RowHeightDelegate : public QStyledItemDelegate {
public:
    RowHeightDelegate(int rowHeight, QObject *parent)
        : QStyledItemDelegate(parent)
        , rowHeight_(rowHeight)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), rowHeight_));
        return size;
    }

private:
    int rowHeight_;
};

} // namespace

void applyTreeMetrics(QTreeView *tree, const QFont &editorFont)
{
    QFont font = QApplication::font();
    font.setPointSizeF(std::max(font.pointSizeF(), editorFont.pointSizeF()));
    tree->setFont(font);

    const int textHeight = QFontMetrics(font).height();
    tree->setIconSize(QSize(textHeight, textHeight));
    tree->setIndentation(qRound(textHeight * indentRatio));

    // Only the standard delegate is replaced: a custom one (file tree,
    // changes, history…) paints and sizes its rows itself, and code may
    // rely on its type.
    QAbstractItemDelegate *previous = tree->itemDelegate();
    // The exact type: subclasses without Q_OBJECT share the base's metaObject.
    if (previous && typeid(*previous) != typeid(QStyledItemDelegate))
        return;
    tree->setItemDelegate(new RowHeightDelegate(treeRowHeight(font), tree));
    if (previous && previous->parent() == tree)
        delete previous;
}

int treeRowHeight(const QFont &font)
{
    return qRound(QFontMetrics(font).height() * rowRatio);
}
