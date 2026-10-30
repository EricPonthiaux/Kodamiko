// SPDX-License-Identifier: GPL-3.0-only

#include "Mindmap.h"
#include "viewers/diagram/Board.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <array>
#include <deque>

namespace diagram::mindmap {
namespace {

constexpr qreal ideaGap = 20;
constexpr qreal topicGap = 56;
constexpr qreal columnGap = 64;
constexpr qreal centerGap = 96;

// A spanning forest also gives malformed inputs (cycles, shared children,
// disconnected nodes) a finite layout without dropping any content.
class Tree {
public:
    explicit Tree(const Board &board)
    {
        QSet<QString> targets;
        for (const auto &link : board.links()) {
            if (link->from == link->to)
                continue;
            if (Shape *child = board.shape(link->to)) {
                edges_[link->from].append(child);
                targets.insert(child->id);
            }
        }
        for (const auto &node : board.shapes()) {
            if (!targets.contains(node->id))
                visit(node.get());
        }
        for (const auto &node : board.shapes())
            visit(node.get());
    }

    QList<Shape *> roots;
    QList<Shape *> order;
    QHash<QString, QList<Shape *>> children;

private:
    void visit(Shape *root)
    {
        if (seen_.contains(root->id))
            return;
        roots.append(root);
        seen_.insert(root->id);
        std::deque<Shape *> queue{root};
        while (!queue.empty()) {
            Shape *parent = queue.front();
            queue.pop_front();
            order.append(parent);
            for (Shape *child : edges_.value(parent->id)) {
                if (seen_.contains(child->id))
                    continue;
                seen_.insert(child->id);
                children[parent->id].append(child);
                queue.push_back(child);
            }
        }
    }

    QHash<QString, QList<Shape *>> edges_;
    QSet<QString> seen_;
};

qreal totalHeight(const QList<Shape *> &nodes, const QHash<QString, qreal> &heights, qreal gap)
{
    qreal height = std::max<qsizetype>(0, nodes.size() - 1) * gap;
    for (const Shape *node : nodes)
        height += heights.value(node->id, node->h);
    return height;
}

QHash<QString, qreal> subtreeHeights(const Tree &tree)
{
    QHash<QString, qreal> heights;
    for (auto it = tree.order.crbegin(); it != tree.order.crend(); ++it) {
        const Shape *node = *it;
        const qreal children = totalHeight(tree.children.value(node->id), heights, ideaGap);
        heights.insert(node->id, std::max(node->h, children));
    }
    return heights;
}

struct Placement {
    Shape *node;
    qreal edge;
    qreal top;
    int direction;
};

class Layout {
public:
    explicit Layout(const Tree &tree)
        : tree_(tree)
        , heights_(subtreeHeights(tree))
    {
    }

    qreal placeRoot(Shape &root, qreal top)
    {
        const auto sides = balance(tree_.children.value(root.id));
        const qreal height = std::max({root.h, sideHeight(sides[0]), sideHeight(sides[1])});
        const qreal middle = top + height / 2;
        root.x = -root.w / 2;
        root.y = middle - root.h / 2;
        placeSide(sides[0], root.x - centerGap, middle, -1);
        placeSide(sides[1], root.x + root.w + centerGap, middle, 1);
        return height;
    }

private:
    qreal sideHeight(const QList<Shape *> &nodes) const { return totalHeight(nodes, heights_, topicGap); }

    std::array<QList<Shape *>, 2> balance(const QList<Shape *> &topics) const
    {
        std::array<QList<Shape *>, 2> sides;
        std::array<qreal, 2> heights{};
        for (Shape *topic : topics) {
            const size_t side = heights[0] <= heights[1] ? 0 : 1;
            sides[side].append(topic);
            heights[side] += heights_.value(topic->id) + topicGap;
        }
        return sides;
    }

    void placeSide(const QList<Shape *> &topics, qreal edge, qreal middle, int direction)
    {
        qreal top = middle - sideHeight(topics) / 2;
        for (Shape *topic : topics) {
            placeBranch({topic, edge, top, direction});
            top += heights_.value(topic->id) + topicGap;
        }
    }

    void placeBranch(Placement start)
    {
        std::deque<Placement> queue{start};
        while (!queue.empty()) {
            const Placement place = queue.front();
            queue.pop_front();
            Shape &node = *place.node;
            const qreal height = heights_.value(node.id);
            node.x = place.direction > 0 ? place.edge : place.edge - node.w;
            node.y = place.top + (height - node.h) / 2;
            const QList<Shape *> children = tree_.children.value(node.id);
            qreal top = place.top + (height - totalHeight(children, heights_, ideaGap)) / 2;
            const qreal edge = place.direction > 0 ? node.x + node.w + columnGap : node.x - columnGap;
            for (Shape *child : children) {
                queue.push_back({child, edge, top, place.direction});
                top += heights_.value(child->id) + ideaGap;
            }
        }
    }

    const Tree &tree_;
    QHash<QString, qreal> heights_;
};

} // namespace

void arrange(Board &board)
{
    board.resolveLabels();
    const Tree tree(board);
    Layout layout(tree);
    qreal top = 0;
    for (Shape *root : tree.roots)
        top += layout.placeRoot(*root, top) + centerGap;
    for (const auto &link : board.links()) {
        link->spline.reset();
        link->layoutLabelPoint.reset();
    }
}

} // namespace diagram::mindmap
