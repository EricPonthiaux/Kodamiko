// SPDX-License-Identifier: GPL-3.0-only

#include "Lanes.h"
#include "Sequence.h"
#include "viewers/diagram/Board.h"

#include <algorithm>
#include <optional>

namespace diagram::sequence::detail {

QList<ActivationSpan> activationSpans(const Board &board, const QString &participant)
{
    QList<int> starts;
    QList<ActivationSpan> spans;
    int last = 0;
    for (const MessageLink *message : orderedMessages(board)) {
        const int order = message->order.value_or(0);
        last = std::max(last, order);
        if (message->activation == QLatin1String("+") && message->to == participant)
            starts.append(order);
        if (message->activation == QLatin1String("-") && message->from == participant && !starts.isEmpty()) {
            const int start = starts.takeLast();
            spans.append({start, order, int(starts.size())});
        }
    }
    while (!starts.isEmpty()) {
        const int start = starts.takeLast();
        spans.append({start, last, int(starts.size())});
    }
    // Outer bars are painted first so nested activations remain visible.
    std::stable_sort(spans.begin(), spans.end(),
                     [](const ActivationSpan &a, const ActivationSpan &b) { return a.depth < b.depth; });
    return spans;
}

qreal activationEdge(const Board &board, const QString &participant, int order, qreal direction)
{
    std::optional<int> depth;
    for (const ActivationSpan &span : activationSpans(board, participant)) {
        if (span.start <= order && order <= span.end)
            depth = std::max(depth.value_or(0), span.depth);
    }
    return depth ? *depth * 4 + direction * 5 : 0;
}

std::pair<qreal, qreal> selfActivationEdges(const Board &board, const MessageLink &message)
{
    const int order = message.order.value_or(0);
    std::optional<int> fromDepth;
    std::optional<int> toDepth;
    for (const ActivationSpan &span : activationSpans(board, message.from)) {
        if (span.start > order || span.end < order)
            continue;
        const bool opening = message.activation == QLatin1String("+") && span.start == order;
        const bool closing = message.activation == QLatin1String("-") && span.end == order;
        if (!opening)
            fromDepth = std::max(fromDepth.value_or(0), span.depth);
        if (!closing)
            toDepth = std::max(toDepth.value_or(0), span.depth);
    }
    return {fromDepth ? *fromDepth * 4 + 5 : 0, toDepth ? *toDepth * 4 + 5 : 0};
}

qreal activationBottom(const Board &board, const ActivationSpan &span)
{
    const qreal lane = messageY(board, span.end);
    for (const MessageLink *message : messages(board)) {
        if (message->order.value_or(0) == span.end && message->from == message->to)
            return lane + selfLoopHeight;
    }
    return lane;
}

} // namespace diagram::sequence::detail
