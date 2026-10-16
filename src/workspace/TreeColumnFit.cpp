#include "TreeColumnFit.h"

#include <QEvent>
#include <QHeaderView>
#include <QTimer>
#include <QTreeView>

#include <algorithm>

TreeColumnFit::TreeColumnFit(QTreeView *tree)
    : QObject(tree)
    , tree_(tree)
    , timer_(new QTimer(this))
{
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    tree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    timer_->setSingleShot(true);
    timer_->setInterval(0);
    connect(timer_, &QTimer::timeout, this, &TreeColumnFit::fit);

    connect(tree, &QTreeView::expanded, this, &TreeColumnFit::refit);
    connect(tree, &QTreeView::collapsed, this, &TreeColumnFit::refit);
    QAbstractItemModel *model = tree->model();
    connect(model, &QAbstractItemModel::rowsInserted, this, &TreeColumnFit::refit);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &TreeColumnFit::refit);
    connect(model, &QAbstractItemModel::modelReset, this, &TreeColumnFit::refit);
    connect(model, &QAbstractItemModel::layoutChanged, this, &TreeColumnFit::refit);
    connect(model, &QAbstractItemModel::dataChanged, this, &TreeColumnFit::refit);
    tree->installEventFilter(this);           // font, style
    tree->viewport()->installEventFilter(this); // size
    refit();
}

void TreeColumnFit::refit()
{
    timer_->start();
}

bool TreeColumnFit::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::FontChange:
    case QEvent::StyleChange:
        refit();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void TreeColumnFit::fit()
{
    // Protected in QTreeView, public in QAbstractItemView: the same virtual
    // function, which first runs any pending layout.
    const QAbstractItemView *view = tree_;
    const int width = std::max(tree_->viewport()->width(), view->sizeHintForColumn(0));
    if (tree_->header()->sectionSize(0) != width)
        tree_->header()->resizeSection(0, width);
}
