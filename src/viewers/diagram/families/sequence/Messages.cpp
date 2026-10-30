// SPDX-License-Identifier: GPL-3.0-only

#include "Lanes.h"
#include "Sequence.h"
#include "viewers/diagram/Board.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace diagram::sequence {

using namespace detail;

std::optional<Route> MessageLink::route(const Board &board) const
{
    const Shape *a = board.shape(from);
    const Shape *b = board.shape(to);
    if (!a || !b)
        return std::nullopt;
    if (!order)
        order = maxOrder(board) + 1;
    const qreal lane = messageY(board, *order);
    Route r;
    const qreal direction = from == to || b->cx() >= a->cx() ? 1 : -1;
    r.a = QPointF(a->cx() + activationEdge(board, from, *order, direction), lane);
    if (from == to) {
        const auto [start, end] = selfActivationEdges(board, *this);
        r.a.setX(a->cx() + start);
        r.b = QPointF(a->cx() + end, lane + selfLoopHeight);
        r.points = {r.a, QPointF(a->cx() + selfLoopWidth, lane),
                    QPointF(a->cx() + selfLoopWidth, lane + selfLoopHeight), r.b};
        return r;
    }
    r.b = QPointF(b->cx() + activationEdge(board, to, *order, -direction), lane);
    return r;
}

Link::LabelBox MessageLink::labelBox(const Board &) const
{
    LabelBox box;
    box.lines = label.split(QLatin1Char('\n'));
    box.lineHeight = messageLineHeight;
    box.width = text::widest(messageFont, box.lines) + 12;
    box.height = box.lines.size() * box.lineHeight + 6;
    return box;
}

// Labels clear the shaft; a self-call's label starts beyond its loop.
QPointF MessageLink::labelPos(const Route &r, const Board &board) const
{
    const LabelBox box = labelBox(board);
    if (from == to)
        return {r.a.x() + selfLoopWidth + 12 + box.width / 2, r.a.y() + selfLoopHeight / 2};
    return {(r.a.x() + r.b.x()) / 2, r.a.y() - 8 - box.height / 2};
}

bool MessageLink::hitTest(const Board &board, QPointF point, qreal tolerance) const
{
    const auto r = route(board);
    if (!r)
        return false;
    const LabelBox box = labelBox(board);
    const QRectF rect(labelPos(*r, board) - QPointF(box.width / 2, box.height / 2), QSizeF(box.width, box.height));
    return (!label.isEmpty() && rect.contains(point)) || distanceToRoute(*r, point) <= tolerance;
}

LinkStyle MessageLink::drawStyle(const Board &board, bool selected) const
{
    LinkStyle style = Link::drawStyle(board, selected, 1.65);
    if (!selected && !parseColor(customColor).isValid())
        style.color = board.style().sequence.line;
    return style;
}

void MessageLink::drawLabel(QPainter &painter, const Board &board, bool editing) const
{
    const auto r = route(board);
    if (!r || label.isEmpty() || editing)
        return;
    const Style &style = board.style();
    const LabelBox box = labelBox(board);
    const QPointF center = labelPos(*r, board);
    const QRectF rect(center - QPointF(box.width / 2, box.height / 2), QSizeF(box.width, box.height));
    QColor paper = fragments.isEmpty() ? style.background : style.sequence.fragment;
    if (board.selectedLink() == this)
        paper = mix(paper, style.shape.selectedStroke, 0.16);
    painter.fillPath(roundRect(rect, 3), paper);
    const QColor preferred = line == QLatin1String("dashed") ? style.sequence.mutedInk : style.sequence.ink;
    const QColor ink = readableTextColor(paper, preferred, preferred);
    qreal top = center.y() - (box.lines.size() - 1) * box.lineHeight / 2;
    for (const QString &text : box.lines) {
        text::draw(painter, messageFont, text, QPointF(center.x(), top), ink, Align::Center, Baseline::Middle);
        top += box.lineHeight;
    }
}

void MessageLink::onDoubleClick(Board &board)
{
    TextEditSpec spec;
    spec.get = [this] { return label; };
    spec.set = [this](const QString &value) { label = value; };
    spec.color = board.style().sequence.ink;
    spec.font = messageFont;
    spec.lineHeight = messageLineHeight;
    spec.align = Align::Center;
    spec.rect = [this, &board] {
        const auto r = route(board);
        if (!r)
            return QRectF();
        const LabelBox box = labelBox(board);
        const QPointF center = labelPos(*r, board);
        const qreal width = std::max<qreal>(50, box.width);
        return QRectF(center - QPointF(width / 2, box.height / 2), QSizeF(width, box.height));
    };
    board.beginTextEdit(this, spec);
}

void MessageLink::draw(QPainter &painter, const Board &board, bool selected) const
{
    const std::optional<Route> r = route(board);
    if (!r)
        return;
    const LinkStyle style = drawStyle(board, selected);
    BodyOptions body;
    body.color = style.color;
    body.width = style.width;
    if (line == QLatin1String("dashed"))
        body.dash = {6, 4};
    body.trim = head == QLatin1String("arrow") ? arrowBaseInset(style.arrow) : 0;
    body.trimStart = startHead == QLatin1String("arrow") ? arrowBaseInset(style.arrow) : 0;
    strokeBody(painter, *r, body);
    drawHead(painter, board, style, {head, r->b, arrivalAngle(*r)});
    if (startHead != QLatin1String("none"))
        drawHead(painter, board, style, {startHead, r->a, departureAngle(*r) + M_PI});
}

void MessageLink::drawHead(QPainter &painter,
                           const Board &board,
                           const LinkStyle &style,
                           const HeadPlacement &placement) const
{
    const QString &kind = placement.kind;
    const QPointF tip = placement.tip;
    const qreal angle = placement.angle;
    if (kind == QLatin1String("arrow")) {
        fillArrowHead(painter, tip, angle, style.arrow, style.color);
    } else if (kind == QLatin1String("open")) {
        const qreal spread = 0.5;
        const qreal length = (board.style().link.arrow + 2) * style.markerScale;
        QPainterPath path;
        path.moveTo(tip.x() - length * std::cos(angle - spread), tip.y() - length * std::sin(angle - spread));
        path.lineTo(tip);
        path.lineTo(tip.x() - length * std::cos(angle + spread), tip.y() - length * std::sin(angle + spread));
        stroke(painter, path, style.color, style.width);
    } else if (kind == QLatin1String("cross")) {
        const qreal s = 5 * style.markerScale;
        QPainterPath cross;
        cross.moveTo(tip.x() - s, tip.y() - s);
        cross.lineTo(tip.x() + s, tip.y() + s);
        cross.moveTo(tip.x() - s, tip.y() + s);
        cross.lineTo(tip.x() + s, tip.y() - s);
        stroke(painter, cross, style.color, style.width);
    }
}

} // namespace diagram::sequence
