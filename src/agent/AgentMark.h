// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QColor>
#include <QRectF>

#include <chrono>
#include <optional>

class QPainter;

// What the agent's indicator shows (see AgentIndicator).
enum class AgentStatus {
    Ready,    // nothing in progress, nothing new
    Working,  // a response is in progress
    NeedsYou, // the agent waits for an approval or an answer
    Done,     // a response is ready, not seen yet
};

// A moment of the mark's animation.
struct AgentMarkFrame {
    AgentStatus status = AgentStatus::Ready;
    int index = 0; // frames since the status changed, modulo loopFrames()
};

// The agents' mark in their indicator, and how it moves (see UiIcons'
// "claude"): a ring of eight dots, the large ones on the axes, a neutral
// motif shared by every agent. While the agent works, each dot lights up in
// turn; while it waits for the user, the whole ring breathes; when a
// response arrives, the large and the small dots flash in turn, then the
// ring shines still. The indicator owns the timer and counts the frames;
// the mark says how fast they go, when they stop, and what each one looks
// like.
namespace AgentMark {

// The time between two frames of `status`'s animation; nullopt when the
// mark stays still.
std::optional<std::chrono::milliseconds> frameStep(AgentStatus status);
// The frame counter goes back to 0 after this many frames: a multiple of
// the periods of the looping animations.
int loopFrames();
// Whether a one-off animation (a response arriving…) is over at `frame`.
bool isFinished(AgentMarkFrame frame);
// Draws the mark at `frame` in `box`, in `color`. With motion off
// (Motion::policy()), the timer does not run: the mark draws a still form
// that tells the state apart by its color alone.
void paint(QPainter &painter, const QRectF &box, const QColor &color, AgentMarkFrame frame);

} // namespace AgentMark
