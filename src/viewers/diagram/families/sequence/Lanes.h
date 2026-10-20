// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>
#include <QtGlobal>

namespace diagram {
class Board;
}

// What the participants, the messages and the layout share about the
// message lanes.
namespace diagram::sequence {

class MessageLink;

namespace detail {

constexpr qreal selfLoopWidth = 56; // horizontal reach of a self-message loop

QList<const MessageLink *> messages(const Board &board);
// Messages in conversation order (a message without a rank counts as 0, as
// sidecar's `order ?? 0`).
QList<const MessageLink *> orderedMessages(const Board &board);
// The highest rank given to a message; -1 when none has one.
int maxOrder(const Board &board);

} // namespace detail

} // namespace diagram::sequence
