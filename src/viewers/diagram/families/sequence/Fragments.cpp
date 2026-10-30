// SPDX-License-Identifier: GPL-3.0-only

#include "Lanes.h"
#include "Sequence.h"
#include "viewers/diagram/Board.h"

#include <QHash>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <limits>

namespace diagram::sequence {

using namespace detail;

namespace {

constexpr Font kindFont(14, 600);
constexpr Font conditionFont(13, 400, false, true);
constexpr qreal conditionLineHeight = 18;

struct Branch {
    const MessageLink *first = nullptr;
    Fragment fragment;
};

struct Span {
    Fragment fragment;
    const MessageLink *first = nullptr;
    const MessageLink *last = nullptr;
    QList<Branch> branches;
};

bool sameBranch(const Fragment &a, const Fragment &b)
{
    return a.branchKeyword == b.branchKeyword && a.branchLabel == b.branchLabel;
}

QList<Span> spans(const Board &board)
{
    QList<Span> result;
    QHash<QString, qsizetype> index;
    for (const MessageLink *message : orderedMessages(board)) {
        for (const Fragment &fragment : message->fragments) {
            if (!index.contains(fragment.id)) {
                index.insert(fragment.id, result.size());
                result.append({fragment, message, message, {}});
            }
            Span &span = result[index.value(fragment.id)];
            span.last = message;
            const Fragment &previous = span.branches.isEmpty() ? span.fragment : span.branches.last().fragment;
            if (!sameBranch(previous, fragment))
                span.branches.append({message, fragment});
        }
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const Span &a, const Span &b) { return a.fragment.depth < b.fragment.depth; });
    return result;
}

QRectF horizontalExtent(const Board &board, int depth)
{
    qreal left = std::numeric_limits<qreal>::infinity();
    qreal right = -left;
    for (const auto &shape : board.shapes()) {
        left = std::min(left, shape->x + 18);
        right = std::max(right, shape->x + shape->w - 18);
    }
    for (const MessageLink *message : messages(board)) {
        if (message->from != message->to)
            continue;
        if (const Shape *shape = board.shape(message->from))
            right = std::max(right, shape->cx() + selfLoopWidth + message->labelBox(board).width + 40);
    }
    if (!std::isfinite(left))
        return {};
    const qreal inset = depth * 8;
    return {left + inset, 0, std::max<qreal>(120, right - left - inset * 2), 0};
}

qreal tabWidth(const Fragment &fragment)
{
    return std::max<qreal>(48, text::width(kindFont, fragment.kind) + 24);
}

QString condition(const Fragment &fragment, const QString &label)
{
    if (label.isEmpty())
        return {};
    if (fragment.kind == QLatin1String("group") || fragment.kind == QLatin1String("rect"))
        return label;
    return QStringLiteral("[%1]").arg(label);
}

QStringList conditionLines(const Board &board, const Fragment &fragment, const QString &label)
{
    const qreal width = horizontalExtent(board, fragment.depth).width() - tabWidth(fragment) - 32;
    return text::wrap(conditionFont, condition(fragment, label), std::max<qreal>(40, width));
}

qreal labelTop(const Board &board, const MessageLink &message)
{
    return messageY(board, message.order.value_or(0)) - message.labelBox(board).height - 8;
}

qreal nestedHeaders(const Board &board, const MessageLink &message, int depth)
{
    qreal height = 0;
    for (const Fragment &fragment : message.fragments) {
        if (fragment.depth > depth)
            height += fragmentHeaderHeight(board, fragment);
    }
    return height;
}

QRectF frameRect(const Board &board, const Span &span)
{
    QRectF rect = horizontalExtent(board, span.fragment.depth);
    const qreal top = labelTop(board, *span.first) - fragmentHeaderHeight(board, span.fragment) -
                      nestedHeaders(board, *span.first, span.fragment.depth);
    const int children = std::max(0, int(span.last->fragments.size()) - span.fragment.depth - 1);
    qreal tail = 24;
    if (span.last->from == span.last->to)
        tail = std::max(tail, span.last->labelBox(board).height / 2 + selfLoopHeight / 2 + 8);
    rect.setTop(top);
    rect.setBottom(messageY(board, span.last->order.value_or(0)) + tail + children * 12);
    return rect;
}

qreal branchTop(const Board &board, const Branch &branch)
{
    return labelTop(board, *branch.first) - branchHeaderHeight(board, branch.fragment) -
           nestedHeaders(board, *branch.first, branch.fragment.depth);
}

void drawCondition(QPainter &painter, const Board &board, const Branch &heading, const QRectF &rect)
{
    const Style &style = board.style();
    const QStringList lines = conditionLines(board, heading.fragment, heading.fragment.label);
    const qreal left = rect.left() + tabWidth(heading.fragment) + 14;
    const qreal width = text::widest(conditionFont, lines) + 8;
    painter.fillRect(QRectF(left - 4, rect.top() + 5, width, lines.size() * conditionLineHeight + 4),
                     style.sequence.fragment);
    qreal top = rect.top() + 8;
    for (const QString &line : lines) {
        text::draw(painter, conditionFont, line, QPointF(left, top), style.sequence.mutedInk, Align::Left,
                   Baseline::Top);
        top += conditionLineHeight;
    }
}

void drawTab(QPainter &painter, const Style &style, const Fragment &fragment, const QRectF &rect)
{
    const qreal left = rect.left();
    const qreal top = rect.top();
    const qreal width = tabWidth(fragment);
    const auto path = polygon(
        {{left, top}, {left + width, top}, {left + width, top + 20}, {left + width - 10, top + 30}, {left, top + 30}});
    painter.fillPath(path, style.sequence.header);
    stroke(painter, path, style.sequence.fragmentStroke, 1.1);
    const QColor ink = readableTextColor(style.sequence.header, style.sequence.ink, style.sequence.ink);
    text::draw(painter, kindFont, fragment.kind, QPointF(left + 12, top + 15), ink, Align::Left, Baseline::Middle);
}

} // namespace

namespace detail {

qreal fragmentHeaderHeight(const Board &board, const Fragment &fragment)
{
    return std::max<qreal>(36, conditionLines(board, fragment, fragment.label).size() * conditionLineHeight + 18);
}

qreal branchHeaderHeight(const Board &board, const Fragment &fragment)
{
    return std::max<qreal>(36, conditionLines(board, fragment, fragment.branchLabel).size() * conditionLineHeight + 18);
}

std::optional<QRectF> fragmentBounds(const Board &board)
{
    std::optional<QRectF> bounds;
    for (const Span &span : spans(board)) {
        const QRectF rect = frameRect(board, span);
        bounds = bounds ? bounds->united(rect) : rect;
    }
    return bounds;
}

} // namespace detail

void drawFragments(QPainter &painter, const Board &board)
{
    const Style &style = board.style();
    for (const Span &span : spans(board)) {
        const QRectF rect = frameRect(board, span);
        const QPainterPath path = roundRect(rect, 3);
        painter.fillPath(path, style.sequence.fragment);
        stroke(painter, path, style.sequence.fragmentStroke, 1.1);
        for (const Branch &branch : span.branches) {
            const qreal y = branchTop(board, branch);
            strokeLine(painter, QPointF(rect.left(), y), QPointF(rect.right(), y), style.sequence.fragmentStroke, 1,
                       {6, 5});
        }
    }
}

void drawFragmentLabels(QPainter &painter, const Board &board)
{
    for (const Span &span : spans(board)) {
        const QRectF rect = frameRect(board, span);
        drawTab(painter, board.style(), span.fragment, rect);
        drawCondition(painter, board, {span.first, span.fragment}, rect);
        for (const Branch &branch : span.branches) {
            QRectF area = rect;
            area.moveTop(branchTop(board, branch));
            Fragment fragment = branch.fragment;
            fragment.label = fragment.branchLabel;
            drawCondition(painter, board, {branch.first, fragment}, area);
        }
    }
}

} // namespace diagram::sequence
