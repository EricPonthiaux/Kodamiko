// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "viewers/diagram/Style.h"

#include <QList>
#include <QtGlobal>

#include <optional>
#include <utility>

namespace diagram {
class Board;
}

// What the participants, the messages and the layout share about the
// message lanes.
namespace diagram::sequence {

class MessageLink;
struct Fragment;

namespace detail {

constexpr qreal selfLoopWidth = 56; // horizontal reach of a self-message loop
constexpr qreal selfLoopHeight = 18;
constexpr Font messageFont(14, 400, false, true);
constexpr qreal messageLineHeight = 20;

struct ActivationSpan {
    int start = 0;
    int end = 0;
    int depth = 0;
};

// The intervals used by both the bars and the arrows that meet their edges.
QList<ActivationSpan> activationSpans(const Board &board, const QString &participant);
qreal activationEdge(const Board &board, const QString &participant, int order, qreal direction);
std::pair<qreal, qreal> selfActivationEdges(const Board &board, const MessageLink &message);
qreal activationBottom(const Board &board, const ActivationSpan &span);
qreal fragmentHeaderHeight(const Board &board, const Fragment &fragment);
qreal branchHeaderHeight(const Board &board, const Fragment &fragment);
std::optional<QRectF> fragmentBounds(const Board &board);

QList<const MessageLink *> messages(const Board &board);
// Messages in conversation order (a message without a rank counts as 0, as
// sidecar's `order ?? 0`).
QList<const MessageLink *> orderedMessages(const Board &board);
// The highest rank given to a message; -1 when none has one.
int maxOrder(const Board &board);

} // namespace detail

} // namespace diagram::sequence
