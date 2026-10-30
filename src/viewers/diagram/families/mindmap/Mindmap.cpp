// SPDX-License-Identifier: GPL-3.0-only

#include "Mindmap.h"

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/families/Families.h"

#include <QCoreApplication>
#include <QHash>
#include <QPainter>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>

namespace diagram::mindmap {

namespace {

// Per presentation level: root, depth 1, depth 2+.
const std::array<Font, 3> fonts{Font(22, 600), Font(17, 550), Font(14)};
constexpr std::array<qreal, 3> lineHeights{29, 24, 21};
constexpr std::array<qreal, 3> padX{28, 20, 12};
constexpr std::array<qreal, 3> padY{20, 14, 8};
constexpr std::array<qreal, 3> maxTextWidths{260, 210, 190};

QString tr(const char *text)
{
    return QCoreApplication::translate("MindmapDiagram", text);
}

QPointF sideAnchor(const Shape &shape, qreal direction)
{
    const auto *node = dynamic_cast<const MindNode *>(&shape);
    const bool leaf = node && node->depth >= 2 && !parseColor(shape.customFill).isValid();
    return {direction > 0 ? shape.x + shape.w : shape.x, leaf ? shape.y + shape.h : shape.cy()};
}

Route branchRoute(const Shape &a, const Shape &b)
{
    Route result;
    const bool horizontal = a.x + a.w <= b.x || b.x + b.w <= a.x;
    if (horizontal) {
        const qreal direction = b.cx() >= a.cx() ? 1 : -1;
        result.a = sideAnchor(a, direction);
        result.b = sideAnchor(b, -direction);
        const qreal bend = (result.b.x() - result.a.x()) / 2;
        result.curve = {result.a, result.a + QPointF(bend, 0), result.b - QPointF(bend, 0), result.b};
    } else {
        const qreal direction = b.cy() >= a.cy() ? 1 : -1;
        result.a = {a.cx(), direction > 0 ? a.y + a.h : a.y};
        result.b = {b.cx(), direction > 0 ? b.y : b.y + b.h};
        const qreal bend = (result.b.y() - result.a.y()) / 2;
        result.curve = {result.a, result.a + QPointF(0, bend), result.b - QPointF(0, bend), result.b};
    }
    return result;
}

} // namespace

int level(int depth)
{
    return std::clamp(depth, 0, 2);
}

// ---------------------------------------------------------------------------
// MindNode

MindNode::MindNode()
    : Shape(QStringLiteral("mind"))
{
}

void MindNode::load(const QJsonObject &props)
{
    Shape::load(props);
    text = jsonString(props, QStringLiteral("text"));
    depth = std::max(0, int(jsonNumber(props, QStringLiteral("depth"), 1)));
}

QJsonObject MindNode::toJson() const
{
    QJsonObject json = Shape::toJson();
    json.insert(QStringLiteral("text"), text);
    json.insert(QStringLiteral("depth"), depth);
    return json;
}

void MindNode::measure(const Board &)
{
    const int l = level(depth);
    lines_ = text::wrap(fonts[l], text, maxTextWidths[l]);
    const qreal widest = std::max<qreal>(1, text::widest(fonts[l], lines_));
    w = widest + padX[l] * 2;
    h = lines_.size() * lineHeights[l] + padY[l] * 2;
}

void MindNode::resizeTo(qreal width, qreal height, const Board &board)
{
    Shape::resizeTo(width, height, board);
    const int l = level(depth);
    // The measured width is the widest line plus the paddings: taking them
    // off again may land a hair under that line and wrap it. Hence the
    // tolerance.
    lines_ = text::wrap(fonts[l], text, std::max<qreal>(20, w - padX[l] * 2) + 0.01);
}

void MindNode::onDoubleClick(Board &board)
{
    const int l = level(depth);
    TextEditSpec spec;
    spec.get = [this] { return text; };
    spec.set = [this](const QString &value) { text = value; };
    spec.color = inkColor(board.style());
    spec.font = fonts[l];
    spec.lineHeight = lineHeights[l];
    spec.align = Align::Center;
    spec.rect = [this, l] {
        const qreal th = std::max<qsizetype>(1, lines_.size()) * lineHeights[l];
        return QRectF(x + padX[l], cy() - th / 2, w - padX[l] * 2, th);
    };
    board.beginTextEdit(this, spec);
}

void MindNode::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const int l = level(depth);
    const Style &style = board.style();
    drawBody(painter, style, state.selected);
    if (state.editing)
        return;
    const QColor ink = inkColor(style);
    qreal ty = cy() - ((std::max<qsizetype>(1, lines_.size()) - 1) * lineHeights[l]) / 2;
    for (const QString &line : lines_) {
        text::draw(painter, fonts[l], line, QPointF(cx(), ty), ink, Align::Center, Baseline::Middle);
        ty += lineHeights[l];
    }
}

QColor MindNode::branchColor(const Style &style) const
{
    if (branchIndex < 0)
        return style.mindmap.rootStroke;
    return style.mindmap.branches[size_t(branchIndex) % style.mindmap.branches.size()];
}

QColor MindNode::bodyColor(const Style &style) const
{
    if (depth == 0)
        return fillColor(style.mindmap.rootFill);
    return fillColor(mix(style.palette.paper, branchColor(style), style.mindmap.fillTint));
}

QColor MindNode::inkColor(const Style &style) const
{
    const bool openLeaf = depth >= 2 && !parseColor(customFill).isValid();
    const QColor paper = openLeaf ? style.palette.paper : bodyColor(style);
    return readableTextColor(paper, style.palette.ink, style.palette.ink);
}

void MindNode::drawBody(QPainter &painter, const Style &style, bool selected) const
{
    if (depth >= 2 && !parseColor(customFill).isValid()) {
        drawLeafBody(painter, style, selected);
        return;
    }
    const QPainterPath path = roundRect(rect(), depth == 0 ? 20 : 12);
    painter.fillPath(path, bodyColor(style));
    const QColor edge = selected ? style.shape.selectedStroke : strokeColor(branchColor(style));
    stroke(painter, path, edge, selected ? 2.5 : 1.25);
}

void MindNode::drawLeafBody(QPainter &painter, const Style &style, bool selected) const
{
    // The underline meets the incoming branch and keeps its color.
    strokeLine(painter, QPointF(x, y + h), QPointF(x + w, y + h), branchColor(style), 1.25);
    if (selected)
        stroke(painter, roundRect(x - 2, y - 2, w + 4, h + 4, shapeRadiusSm), style.shape.selectedStroke, 1.5);
}

// ---------------------------------------------------------------------------
// MindLink

MindLink::MindLink()
    : Link(QStringLiteral("mind"))
{
}

qreal MindLink::nativeWidth(const Board &board) const
{
    const auto *source = dynamic_cast<const MindNode *>(board.shape(from));
    return source && source->depth == 0 ? 3.5 : 1.5;
}

QStringList MindLink::graphvizAttributes(const Board &board) const
{
    return {QStringLiteral("arrowhead=none"),
            QStringLiteral("penwidth=") + QString::number(nativeWidth(board), 'f', 3)};
}

LinkStyle MindLink::drawStyle(const Board &board, bool selected) const
{
    LinkStyle style = Link::drawStyle(board, selected, nativeWidth(board));
    const auto *target = dynamic_cast<const MindNode *>(board.shape(to));
    if (target && !parseColor(customColor).isValid() && !selected)
        style.color = target->branchColor(board.style());
    return style;
}

std::optional<Route> MindLink::route(const Board &board) const
{
    const Shape *a = board.shape(from);
    const Shape *b = board.shape(to);
    if (!a || !b)
        return std::nullopt;
    return branchRoute(*a, *b);
}

void MindLink::draw(QPainter &painter, const Board &board, bool selected) const
{
    const std::optional<Route> r = route(board);
    if (!r)
        return;
    const LinkStyle style = drawStyle(board, selected);
    strokeBody(painter, *r, {style.color, style.width, linkDash(*this), 0, 0});
}

} // namespace diagram::mindmap

// ---------------------------------------------------------------------------
// The family

namespace diagram::families {

namespace {

struct Branches {
    QHash<QString, QList<QString>> children;
    QSet<QString> hasParent;
};

Branches branchesOf(const Board &board)
{
    Branches branches;
    for (const auto &l : board.links()) {
        if (l->from == l->to)
            continue;
        branches.children[l->from].append(l->to);
        branches.hasParent.insert(l->to);
    }
    return branches;
}

struct Presentation {
    int depth = 1;
    int branch = -1;
};

QString nodeIdentity(const Board &board, const QString &id)
{
    const Shape *shape = board.shape(id);
    return shape && !shape->key.isEmpty() ? shape->key : id;
}

QList<QString> orderedChildren(const Board &board, QList<QString> children)
{
    std::sort(children.begin(), children.end(),
              [&board](const QString &a, const QString &b) { return nodeIdentity(board, a) < nodeIdentity(board, b); });
    return children;
}

// Breadth first from the nodes nobody points to. A root's child starts a
// color; every descendant inherits it, even when attached interactively.
QHash<QString, Presentation> presentationsFromRoots(const Board &board)
{
    const Branches branches = branchesOf(board);
    QHash<QString, Presentation> presentations;
    std::deque<QString> queue;
    for (const auto &s : board.shapes()) {
        const auto *node = dynamic_cast<const mindmap::MindNode *>(s.get());
        if (node && !branches.hasParent.contains(s->id)) {
            presentations.insert(s->id, {node->depth, -1});
            queue.push_back(s->id);
        }
    }
    while (!queue.empty()) {
        const QString parent = queue.front();
        queue.pop_front();
        const Presentation inherited = presentations.value(parent);
        int nextBranch = 0;
        for (const QString &child : orderedChildren(board, branches.children.value(parent))) {
            if (presentations.contains(child))
                continue;
            const int branch = inherited.depth == 0 ? nextBranch++ : inherited.branch;
            presentations.insert(child, {inherited.depth + 1, branch});
            queue.push_back(child);
        }
    }
    return presentations;
}

// The depth follows the branches: a node attached to another one takes the
// look of the level below it, as the source read again would give it. Nodes
// nobody points to keep theirs (the root, a new idea).
void resolveDepths(const Board &board)
{
    const QHash<QString, Presentation> presentations = presentationsFromRoots(board);
    for (const auto &s : board.shapes()) {
        auto *node = dynamic_cast<mindmap::MindNode *>(s.get());
        if (!node)
            continue;
        const Presentation look = presentations.value(s->id, {node->depth, -1});
        node->branchIndex = look.branch;
        if (look.depth == node->depth)
            continue;
        node->depth = look.depth;
        board.measureShape(*node);
    }
}

} // namespace

const Family &mindmap()
{
    using namespace diagram::mindmap;
    static const Family family = [] {
        Family f;
        f.id = QStringLiteral("mindmap");
        f.title = tr("Mind map");
        f.shapes = {{QStringLiteral("mind"), [] { return std::unique_ptr<Shape>(new MindNode); }}};
        f.links = {{QStringLiteral("mind"), [] { return std::unique_ptr<Link>(new MindLink); }}};
        f.defaultShape = QStringLiteral("mind");
        f.defaultLink = QStringLiteral("mind");
        f.resolveLabels = resolveDepths;
        f.autoArrange = arrange;
        f.preservePlacedShapes = true;
        f.routeLinksOnMove = false;
        return f;
    }();
    return family;
}

} // namespace diagram::families
