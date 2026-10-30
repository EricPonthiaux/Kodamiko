// SPDX-License-Identifier: GPL-3.0-only

#include "States.h"
#include "viewers/diagram/Board.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace diagram::states {

namespace {

constexpr Font labelFont(13, 400, false, true);
constexpr qreal labelLineHeight = 18;

QColor labelPaper(const Board &board, QPointF point)
{
    if (const Shape *container = board.containerAt(point)) {
        const QColor custom = parseColor(container->customFill);
        return custom.isValid() ? custom : board.style().state.container;
    }
    return board.style().background;
}

QRectF labelRect(const TransitionLink &link, const Board &board, const Route &route)
{
    const auto box = link.labelBox(board);
    return {link.labelPos(route, board) - QPointF(box.width / 2, box.height / 2), QSizeF(box.width, box.height)};
}

} // namespace

Link::LabelBox TransitionLink::labelBox(const Board &) const
{
    LabelBox box;
    box.lines = label.split(QLatin1Char('\n'));
    box.lineHeight = labelLineHeight;
    box.width = text::widest(labelFont, box.lines) + 12;
    box.height = box.lines.size() * box.lineHeight + 6;
    return box;
}

QString TransitionLink::graphvizHtmlLabel(const Board &board) const
{
    // Only the measured extent is needed by Graphviz. The canvas paints the
    // text in the user's code font at the returned label position.
    const LabelBox box = labelBox(board);
    return QStringLiteral("<TABLE BORDER=\"0\" CELLBORDER=\"0\" CELLSPACING=\"0\" CELLPADDING=\"0\" "
                          "FIXEDSIZE=\"TRUE\" WIDTH=\"%1\" HEIGHT=\"%2\"><TR><TD></TD></TR></TABLE>")
        .arg(std::max(1, int(std::ceil(box.width))))
        .arg(std::max(1, int(std::ceil(box.height))));
}

bool TransitionLink::hitTest(const Board &board, QPointF point, qreal tolerance) const
{
    const auto r = route(board);
    if (!r)
        return false;
    return (!label.isEmpty() && labelRect(*this, board, *r).contains(point)) || distanceToRoute(*r, point) <= tolerance;
}

void TransitionLink::drawLabel(QPainter &painter, const Board &board, bool editing) const
{
    if (label.isEmpty() || editing)
        return;
    const auto r = route(board);
    if (!r)
        return;
    const LabelBox box = labelBox(board);
    const QRectF rect = labelRect(*this, board, *r);
    const QColor paper = labelPaper(board, rect.center());
    painter.fillPath(roundRect(rect, 3), paper);
    const QColor preferred = board.style().state.mutedInk;
    const QColor ink = readableTextColor(paper, preferred, preferred);
    qreal top = rect.center().y() - (box.lines.size() - 1) * box.lineHeight / 2;
    for (const QString &line : box.lines) {
        text::draw(painter, labelFont, line, QPointF(rect.center().x(), top), ink, Align::Center, Baseline::Middle);
        top += box.lineHeight;
    }
}

void TransitionLink::onDoubleClick(Board &board)
{
    TextEditSpec spec;
    spec.get = [this] { return label; };
    spec.set = [this](const QString &value) { label = value; };
    const auto r = route(board);
    const QColor paper = r ? labelPaper(board, labelPos(*r, board)) : board.style().background;
    const QColor preferred = board.style().state.mutedInk;
    spec.color = readableTextColor(paper, preferred, preferred);
    spec.font = labelFont;
    spec.lineHeight = labelLineHeight;
    spec.align = Align::Center;
    spec.rect = [this, &board] {
        const auto route = this->route(board);
        if (!route)
            return QRectF();
        QRectF rect = labelRect(*this, board, *route);
        const QPointF center = rect.center();
        rect.setWidth(std::max<qreal>(50, rect.width()));
        rect.moveCenter(center);
        return rect;
    };
    board.beginTextEdit(this, spec);
}

} // namespace diagram::states
