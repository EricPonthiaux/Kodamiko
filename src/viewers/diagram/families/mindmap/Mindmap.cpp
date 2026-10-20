// SPDX-License-Identifier: GPL-3.0-only

#include "Mindmap.h"

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/families/Families.h"

#include <QCoreApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPathStroker>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>

namespace diagram::mindmap {

namespace {

// Per presentation level: root, depth 1, depth 2+.
const std::array<Font, 3> fonts{Font(16, 600), Font(14, 500), Font(13)};
constexpr std::array<qreal, 3> lineHeights{23, 20, 19};
constexpr std::array<qreal, 3> padX{24, 18, 10};
constexpr std::array<qreal, 3> padY{16, 12, 6};
constexpr std::array<qreal, 3> maxTextWidths{220, 190, 170};

QString tr(const char *text)
{
    return QCoreApplication::translate("MindmapDiagram", text);
}

// The text's native color: white on the root's solid fill, the ink on the
// bubbles, muted on the leaves.
QColor nativeInkOf(int depth, const Palette &palette)
{
    if (depth == 0)
        return {Qt::white};
    return depth == 1 ? palette.ink : palette.inkMuted;
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
    spec.color = depth == 0 ? QColor(Qt::white) : board.style().palette.ink;
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
    const QColor ink = textColor(nativeInkOf(depth, style.palette));
    qreal ty = cy() - ((std::max<qsizetype>(1, lines_.size()) - 1) * lineHeights[l]) / 2;
    for (const QString &line : lines_) {
        text::draw(painter, fonts[l], line, QPointF(cx(), ty), ink, Align::Center, Baseline::Middle);
        ty += lineHeights[l];
    }
}

void MindNode::drawBody(QPainter &painter, const Style &style, bool selected) const
{
    // Every bubble is rounded-sm, like the states and the rest of the board:
    // no fully rounded stadium for the root and the first level.
    const QPainterPath path = roundRect(x, y, w, h, shapeRadiusSm);
    if (!customFill.isEmpty()) {
        shadowFill(painter, path, fillColor(style.palette.fill), style);
        stroke(painter, path, strokeColor(selected ? style.shape.selectedStroke : style.shape.stroke),
               selected ? 2 : shapeBorderWidth);
    } else if (depth == 0) {
        shadowFill(painter, path, style.palette.solid, style);
        if (selected)
            stroke(painter, path, style.shape.selectedStroke, 2);
    } else if (depth == 1) {
        shadowFill(painter, path, style.tones.sage, style);
        stroke(painter, path, selected ? style.shape.selectedStroke : style.shape.stroke,
               selected ? 2 : shapeBorderWidth);
    } else {
        drawLeafBody(painter, style, selected);
    }
}

void MindNode::drawLeafBody(QPainter &painter, const Style &style, bool selected) const
{
    // A leaf: no box, a fine underline carries the text (Mermaid's deepest
    // level). The selection still outlines the hit box so that it stays
    // graspable. The underline gets the short shadow of the bodies.
    QPainterPath underline;
    underline.moveTo(x + 2, y + h);
    underline.lineTo(x + w - 2, y + h);
    QPainterPathStroker stroker;
    stroker.setWidth(1.5);
    stroker.setCapStyle(Qt::RoundCap);
    shadowFill(painter, stroker.createStroke(underline), style.palette.lifeline, style);
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
    return source && source->depth == 0 ? 2.5 : 1.75;
}

QStringList MindLink::graphvizAttributes(const Board &board) const
{
    return {QStringLiteral("arrowhead=none"),
            QStringLiteral("penwidth=") + QString::number(nativeWidth(board), 'f', 3)};
}

LinkStyle MindLink::drawStyle(const Board &board, bool selected) const
{
    LinkStyle style = Link::drawStyle(board, selected, nativeWidth(board));
    // Branches are drawn in the lifeline's soft ink, not the connectors' one.
    if (!parseColor(customColor).isValid() && !selected)
        style.color = board.style().palette.lifeline;
    return style;
}

void MindLink::draw(QPainter &painter, const Board &board, bool selected) const
{
    const std::optional<Route> r = route(board);
    if (!r)
        return;
    const QPointF a = r->a;
    const QPointF b = r->b;
    // Control point: the midpoint pushed slightly aside, an organic bow.
    const QPointF m = (a + b) / 2;
    const qreal dx = b.x() - a.x();
    const qreal dy = b.y() - a.y();
    const qreal length = std::hypot(dx, dy) > 0 ? std::hypot(dx, dy) : 1;
    const qreal bow = std::min<qreal>(length * 0.12, 26);
    const QPointF control(m.x() - (dy / length) * bow, m.y() + (dx / length) * bow);
    QPainterPath path;
    path.moveTo(a);
    path.quadTo(control, b);
    const LinkStyle style = drawStyle(board, selected);
    stroke(painter, path, style.color, style.width, linkDash(*this));
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

// Breadth first from the nodes nobody points to, which keep their depth.
QHash<QString, int> depthsFromRoots(const Board &board)
{
    const Branches branches = branchesOf(board);
    QHash<QString, int> depths;
    std::deque<QString> queue;
    for (const auto &s : board.shapes()) {
        const auto *node = dynamic_cast<const mindmap::MindNode *>(s.get());
        if (node && !branches.hasParent.contains(s->id)) {
            depths.insert(s->id, node->depth);
            queue.push_back(s->id);
        }
    }
    while (!queue.empty()) {
        const QString parent = queue.front();
        queue.pop_front();
        for (const QString &child : branches.children.value(parent)) {
            if (depths.contains(child))
                continue;
            depths.insert(child, depths.value(parent) + 1);
            queue.push_back(child);
        }
    }
    return depths;
}

// The depth follows the branches: a node attached to another one takes the
// look of the level below it, as the source read again would give it. Nodes
// nobody points to keep theirs (the root, a new idea).
void resolveDepths(const Board &board)
{
    const QHash<QString, int> depths = depthsFromRoots(board);
    for (const auto &s : board.shapes()) {
        auto *node = dynamic_cast<mindmap::MindNode *>(s.get());
        if (!node || !depths.contains(s->id) || depths.value(s->id) == node->depth)
            continue;
        node->depth = depths.value(s->id);
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
        return f;
    }();
    return family;
}

} // namespace diagram::families
