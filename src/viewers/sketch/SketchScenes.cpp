// SPDX-License-Identifier: GPL-3.0-only

#include "SketchScenes.h"

#include <array>

namespace SketchScenes {

namespace {

using Kind = SketchStroke::Kind;

// Row `row` of a Table of `count` rows.
struct TableRow {
    QRectF table;
    int count = 1;
    int row = 0;
};

// The size of a journey's score dots and of a state machine's start.
constexpr qreal dotSize = 12;

// A rectangle `width` by `height` centered on (x, y).
QRectF at(qreal x, qreal y, qreal width, qreal height)
{
    return {x - width / 2, y - height / 2, width, height};
}

QPointF top(const QRectF &rect)
{
    return {rect.center().x(), rect.top()};
}

QPointF bottom(const QRectF &rect)
{
    return {rect.center().x(), rect.bottom()};
}

QPointF left(const QRectF &rect)
{
    return {rect.left(), rect.center().y()};
}

QPointF right(const QRectF &rect)
{
    return {rect.right(), rect.center().y()};
}

QPainterPath straight(QPointF from, QPointF to)
{
    QPainterPath path(from);
    path.lineTo(to);
    return path;
}

// A curve that leaves and arrives vertically.
QPainterPath verticalCurve(QPointF from, QPointF to)
{
    const qreal bend = (to.y() - from.y()) / 2;
    QPainterPath path(from);
    path.cubicTo(from + QPointF(0, bend), to - QPointF(0, bend), to);
    return path;
}

// A curve that leaves and arrives horizontally.
QPainterPath horizontalCurve(QPointF from, QPointF to)
{
    const qreal bend = (to.x() - from.x()) / 2;
    QPainterPath path(from);
    path.cubicTo(from + QPointF(bend, 0), to - QPointF(bend, 0), to);
    return path;
}

qreal rowCenterY(const TableRow &row)
{
    const qreal rowHeight = (row.table.height() - sketchHeaderHeight) / row.count;
    return row.table.top() + sketchHeaderHeight + (row.row + 0.5) * rowHeight;
}

// A relationship from the right edge of one row to the left edge of another.
SketchLink join(const TableRow &from, const TableRow &to)
{
    return {.path = horizontalCurve({from.table.right(), rowCenterY(from)}, {to.table.left(), rowCenterY(to)}),
            .end = SketchLinkEnd::CrowFoot};
}

void add(SketchScene &scene, SketchNode node)
{
    scene.nodes.append(node);
    scene.strokes.append({Kind::Node, scene.nodes.size() - 1});
}

void add(SketchScene &scene, SketchLink link)
{
    scene.links.append(std::move(link));
    scene.strokes.append({Kind::Link, scene.links.size() - 1});
}

// A start, two branches meeting on a decision, two ends.
SketchScene flowchart()
{
    const QRectF start = at(240, 22, 92, 26);
    const QRectF stepLeft = at(120, 78, 92, 26);
    const QRectF stepRight = at(360, 78, 92, 26);
    const QRectF decision = at(240, 134, 42, 30);
    const QRectF endLeft = at(130, 194, 92, 26);
    const QRectF endRight = at(350, 194, 92, 26);
    SketchScene scene;
    add(scene, {start, SketchShape::Terminal});
    add(scene, {verticalCurve(bottom(start), top(stepLeft))});
    add(scene, {stepLeft});
    add(scene, {verticalCurve(bottom(start), top(stepRight))});
    add(scene, {stepRight});
    add(scene, {verticalCurve(bottom(stepLeft), top(decision))});
    add(scene, {verticalCurve(bottom(stepRight), top(decision))});
    add(scene, {decision, SketchShape::Decision});
    add(scene, {.path = verticalCurve(bottom(decision), top(endLeft)), .hasLabel = true});
    add(scene, {endLeft});
    add(scene, {.path = verticalCurve(bottom(decision), top(endRight)), .hasLabel = true});
    add(scene, {endRight});
    return scene;
}

// Four participants exchanging calls and their answers.
SketchScene sequence()
{
    constexpr std::array participants{60.0, 180.0, 300.0, 420.0};
    constexpr qreal activationWidth = 8;
    SketchScene scene;
    for (const qreal x : participants)
        add(scene, {QRectF(x - 40, 6, 80, 208), SketchShape::Lifeline});
    const auto message = [&scene](qreal fromX, qreal toX, qreal y, bool isAnswer) {
        // The arrow stops at the edge of the activation bar.
        const qreal toward = toX > fromX ? -activationWidth / 2 : activationWidth / 2;
        add(scene, {.path = straight({fromX, y}, {toX + toward, y}), .isDashed = isAnswer, .hasLabel = true});
    };
    message(participants[0], participants[1], 58, false);
    add(scene, {QRectF(participants[1] - activationWidth / 2, 58, activationWidth, 120), SketchShape::Activation});
    message(participants[1], participants[2], 84, false);
    message(participants[2], participants[3], 110, false);
    message(participants[3], participants[2], 136, true);
    message(participants[2], participants[1], 160, true);
    message(participants[1], participants[0], 192, true);
    return scene;
}

// A base class, two subclasses and an association.
SketchScene classes()
{
    const QRectF base = at(240, 52, 128, 88);
    const QRectF first = at(110, 172, 112, 72);
    const QRectF second = at(370, 172, 112, 72);
    const QRectF associated = at(420, 52, 96, 60);
    SketchScene scene;
    add(scene, {base, SketchShape::Class, 2});
    add(scene, {first, SketchShape::Class, 1});
    add(scene, {.path = verticalCurve(top(first), bottom(base)), .end = SketchLinkEnd::HollowTriangle});
    add(scene, {second, SketchShape::Class, 1});
    add(scene, {.path = verticalCurve(top(second), bottom(base)), .end = SketchLinkEnd::HollowTriangle});
    add(scene, {associated, SketchShape::Class, 1});
    add(scene, {.path = straight(right(base), left(associated)), .hasLabel = true});
    return scene;
}

// A start, four states with a loop between two of them, an end.
SketchScene states()
{
    constexpr QPointF loopOffset{14, 0};
    const QRectF start = at(30, 110, dotSize + 2, dotSize + 2);
    const QRectF idle = at(116, 110, 80, 28);
    const QRectF running = at(250, 46, 92, 28);
    const QRectF paused = at(250, 174, 92, 28);
    const QRectF done = at(384, 110, 80, 28);
    const QRectF end = at(454, 110, 18, 18);
    SketchScene scene;
    add(scene, {start, SketchShape::StartDot});
    add(scene, {straight(right(start), left(idle))});
    add(scene, {idle});
    add(scene, {.path = horizontalCurve(right(idle), left(running)), .hasLabel = true});
    add(scene, {running});
    add(scene, {.path = straight(bottom(running) - loopOffset, top(paused) - loopOffset), .hasLabel = true});
    add(scene, {paused});
    add(scene, {straight(top(paused) + loopOffset, bottom(running) + loopOffset)});
    add(scene, {.path = horizontalCurve(right(running), left(done)), .hasLabel = true});
    add(scene, {done});
    add(scene, {straight(right(done), left(end))});
    add(scene, {end, SketchShape::EndDot});
    return scene;
}

// Three tables, joined from one row to another.
SketchScene dataModel()
{
    const QRectF orders = at(78, 110, 116, 116);
    const QRectF customers = at(240, 58, 116, 84);
    const QRectF lines = at(402, 154, 116, 84);
    SketchScene scene;
    add(scene, {orders, SketchShape::Table, 4});
    add(scene, {customers, SketchShape::Table, 3});
    add(scene, join({orders, 4, 1}, {customers, 3, 0}));
    add(scene, {lines, SketchShape::Table, 3});
    add(scene, join({customers, 3, 2}, {lines, 3, 0}));
    add(scene, join({orders, 4, 3}, {lines, 3, 2}));
    return scene;
}

// A person using a system of three containers.
SketchScene c4()
{
    const QRectF person = at(240, 30, 56, 54);
    const QRectF system(30, 76, 420, 138);
    const QRectF web = at(110, 148, 100, 44);
    const QRectF api = at(240, 148, 100, 44);
    const QRectF database = at(370, 148, 84, 52);
    SketchScene scene;
    add(scene, {person, SketchShape::Person});
    add(scene, {system, SketchShape::Boundary});
    add(scene, {.path = verticalCurve(bottom(person), top(web)), .hasLabel = true});
    add(scene, {web});
    add(scene, {.path = straight(right(web), left(api)), .hasLabel = true});
    add(scene, {api});
    add(scene, {.path = straight(right(api), left(database)), .hasLabel = true});
    add(scene, {database, SketchShape::Cylinder});
    return scene;
}

// A root, four topics, and the subtopics of two of them.
SketchScene mindMap()
{
    const QRectF root = at(240, 110, 90, 42);
    const std::array topics{at(130, 52, 76, 24), at(130, 168, 76, 24), at(350, 52, 76, 24), at(350, 168, 76, 24)};
    SketchScene scene;
    add(scene, {root, SketchShape::Ellipse});
    for (const QRectF &topic : topics) {
        const bool isLeft = topic.center().x() < root.center().x();
        add(scene, {.path = horizontalCurve(isLeft ? left(root) : right(root), isLeft ? right(topic) : left(topic)),
                    .end = SketchLinkEnd::None});
        add(scene, {topic, SketchShape::Terminal});
    }
    const std::array subtopics{std::pair{topics[0], at(34, 30, 48, 20)}, std::pair{topics[0], at(34, 74, 48, 20)},
                               std::pair{topics[3], at(446, 146, 48, 20)}, std::pair{topics[3], at(446, 190, 48, 20)}};
    for (const auto &[topic, subtopic] : subtopics) {
        const bool isLeft = subtopic.center().x() < topic.center().x();
        add(scene,
            {.path = horizontalCurve(isLeft ? left(topic) : right(topic), isLeft ? right(subtopic) : left(subtopic)),
             .end = SketchLinkEnd::None});
        add(scene, {subtopic, SketchShape::Terminal});
    }
    return scene;
}

// Three sections of two tasks, and the mood line through their scores.
SketchScene journey()
{
    constexpr std::array scores{4, 3, 2, 4, 5, 3};
    constexpr qreal firstTaskX = 48;
    constexpr qreal taskSpacing = 72;
    constexpr qreal sectionSpacing = 12;
    SketchScene scene;
    QPointF previousScore;
    for (int task = 0; task < int(scores.size()); ++task) {
        const int section = task / 2;
        const qreal x = firstTaskX + task * taskSpacing + section * sectionSpacing;
        if (task % 2 == 0)
            add(scene, {at(x + taskSpacing / 2, 16, 2 * taskSpacing - 4, 20), SketchShape::Band});
        add(scene, {at(x, 56, 66, 34)});
        const QPointF score(x, 206 - scores.at(std::size_t(task)) * 22);
        add(scene, {at(score.x(), score.y(), dotSize, dotSize), SketchShape::StartDot});
        if (task > 0)
            add(scene, {.path = straight(previousScore, score), .end = SketchLinkEnd::None});
        previousScore = score;
    }
    return scene;
}

// The product in the middle of the cards that describe it.
SketchScene productVision()
{
    const QRectF product = at(240, 110, 124, 64);
    SketchScene scene;
    add(scene, {product, SketchShape::Table, 2});
    for (const qreal y : {40.0, 110.0, 180.0}) {
        for (const qreal x : {76.0, 404.0}) {
            const QRectF card = at(x, y, 120, 54);
            const bool isLeft = x < product.center().x();
            add(scene,
                {.path = horizontalCurve(isLeft ? left(product) : right(product), isLeft ? right(card) : left(card)),
                 .end = SketchLinkEnd::None});
            add(scene, {card, SketchShape::Table, 2});
        }
    }
    return scene;
}

// Three numbered scenes, read from left to right.
SketchScene explainer()
{
    const std::array panels{at(84, 110, 140, 180), at(240, 110, 140, 180), at(396, 110, 140, 180)};
    SketchScene scene;
    for (int i = 0; i < int(panels.size()); ++i) {
        if (i > 0)
            add(scene, {straight(right(panels.at(std::size_t(i - 1))), left(panels.at(std::size_t(i))))});
        add(scene, {panels.at(std::size_t(i)), SketchShape::Panel, i + 1});
    }
    return scene;
}

} // namespace

SketchScene forKind(QStringView kindId)
{
    using Builder = SketchScene (*)();
    static const std::array<std::pair<QStringView, Builder>, 10> builders{{
        {u"flowchart", flowchart},
        {u"sequence", sequence},
        {u"classes", classes},
        {u"states", states},
        {u"datamodel", dataModel},
        {u"c4", c4},
        {u"mindmap", mindMap},
        {u"journey", journey},
        {u"productvision", productVision},
        {u"explainer", explainer},
    }};
    for (const auto &[id, build] : builders) {
        if (id == kindId)
            return build();
    }
    return flowchart();
}

} // namespace SketchScenes
