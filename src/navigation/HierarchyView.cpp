// SPDX-License-Identifier: GPL-3.0-only

#include "HierarchyView.h"

#include "ui/TreeColumnFit.h"
#include "ui/ViewHeader.h"

#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPointer>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// Item data roles.
constexpr int elementRole = Qt::UserRole; // in elements_, -1 for a placeholder
constexpr int loadedRole = Qt::UserRole + 1;

constexpr int noElement = -1;

} // namespace

HierarchyView::HierarchyView(QWidget *parent)
    : QWidget(parent)
    , header_(new ViewHeader(tr("Hierarchy"), this))
    , tree_(new QTreeWidget(this))
    , placeholder_(new QLabel(tr("Call Hierarchy (Ctrl+Alt+H) and\nType Hierarchy (Ctrl+H) show here"), this))
    , stack_(new QStackedWidget(this))
{
    up_ = header_->addButton(QStringLiteral("up"), {});
    down_ = header_->addButton(QStringLiteral("down"), {});
    for (QToolButton *button : {up_, down_})
        button->setCheckable(true);
    connect(up_, &QToolButton::clicked, this, [this] { setDirection(Hierarchy::Direction::Up); });
    connect(down_, &QToolButton::clicked, this, [this] { setDirection(Hierarchy::Direction::Down); });
    updateButtons();

    tree_->header()->hide();
    tree_->setUniformRowHeights(true);
    tree_->setAnimated(false);
    tree_->setExpandsOnDoubleClick(false);
    columnFit_ = new TreeColumnFit(tree_);
    connect(tree_, &QTreeWidget::itemExpanded, this, &HierarchyView::load);
    connect(tree_, &QTreeWidget::itemClicked, this, &HierarchyView::activate);
    connect(tree_, &QTreeWidget::itemActivated, this, &HierarchyView::activate);

    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setEnabled(false);
    stack_->addWidget(placeholder_);
    stack_->addWidget(tree_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addWidget(stack_, 1);
}

void HierarchyView::setIconColor(const QColor &color)
{
    header_->setIconColor(color);
}

void HierarchyView::showHierarchy(Hierarchy hierarchy)
{
    hierarchy_ = std::move(hierarchy);
    direction_ = Hierarchy::Direction::Up;
    rebuild();
}

void HierarchyView::setDirection(Hierarchy::Direction direction)
{
    direction_ = direction;
    rebuild();
}

void HierarchyView::clear()
{
    showHierarchy({});
}

void HierarchyView::updateButtons()
{
    up_->setToolTip(hierarchy_.upName.isEmpty() ? tr("Up") : hierarchy_.upName);
    down_->setToolTip(hierarchy_.downName.isEmpty() ? tr("Down") : hierarchy_.downName);
    up_->setChecked(direction_ == Hierarchy::Direction::Up);
    down_->setChecked(direction_ == Hierarchy::Direction::Down);
}

void HierarchyView::rebuild()
{
    ++generation_;
    elements_.clear();
    elementItems_.clear();
    tree_->clear();
    updateButtons();
    const QString name = direction_ == Hierarchy::Direction::Up ? hierarchy_.upName : hierarchy_.downName;
    header_->setDetail(hierarchy_.roots.isEmpty() ? QString() : QStringLiteral("%1 · %2").arg(hierarchy_.title, name));
    for (const LspProtocol::HierarchyItem &root : std::as_const(hierarchy_.roots))
        addItem(nullptr, root)->setExpanded(true);
    stack_->setCurrentWidget(hierarchy_.roots.isEmpty() ? static_cast<QWidget *>(placeholder_) : tree_);
    columnFit_->refit();
}

QTreeWidgetItem *HierarchyView::addItem(QTreeWidgetItem *parent, const LspProtocol::HierarchyItem &element)
{
    auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
    const QString file = QFileInfo(element.filePath).fileName();
    item->setText(0, QStringLiteral("%1  %2:%3").arg(element.name, file).arg(element.selectionRange.start.line + 1));
    item->setToolTip(0, element.detail.isEmpty() ? element.name : element.detail);
    item->setData(0, elementRole, int(elements_.size()));
    item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator); // known once unfolded
    elements_.push_back(element);
    elementItems_.push_back(item);
    return item;
}

void HierarchyView::load(QTreeWidgetItem *item)
{
    const int element = item->data(0, elementRole).toInt();
    if (element == noElement || item->data(0, loadedRole).toBool() || !hierarchy_.children)
        return;
    item->setData(0, loadedRole, true);
    auto *loading = new QTreeWidgetItem(item, {tr("Searching…")});
    loading->setData(0, elementRole, noElement);
    loading->setDisabled(true);
    hierarchy_.children(direction_, elements_[element],
                        [guard = QPointer<HierarchyView>(this), generation = generation_,
                         element](const QList<LspProtocol::HierarchyItem> &children) {
                            if (guard)
                                guard->fill(generation, element, children);
                        });
}

void HierarchyView::fill(int generation, int element, const QList<LspProtocol::HierarchyItem> &children)
{
    if (generation != generation_)
        return;
    QTreeWidgetItem *item = elementItems_[element];
    qDeleteAll(item->takeChildren()); // "Searching…"
    for (const LspProtocol::HierarchyItem &child : children)
        addItem(item, child);
    if (children.isEmpty())
        item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicatorWhenChildless);
    columnFit_->refit();
}

void HierarchyView::activate(const QTreeWidgetItem *item)
{
    const int element = item ? item->data(0, elementRole).toInt() : noElement;
    if (element == noElement)
        return;
    const LspProtocol::HierarchyItem &target = elements_[element];
    emit elementActivated(target.filePath, target.selectionRange.start.line, target.selectionRange.start.character);
}
