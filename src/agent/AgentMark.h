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

// An agent's mark in its indicator, and how it moves: each provider draws
// its own (Claude's ring of dots…). The indicator owns the timer and counts the
// frames; the mark says how fast they go, when they stop, and what each one
// looks like. A mark is stateless.
class AgentMark {
public:
    virtual ~AgentMark() = default;

    // The time between two frames of `status`'s animation; nullopt when the
    // mark stays still.
    virtual std::optional<std::chrono::milliseconds> frameStep(AgentStatus status) const = 0;
    // The frame counter goes back to 0 after this many frames: a multiple of
    // the periods of the looping animations.
    virtual int loopFrames() const = 0;
    // Whether a one-off animation (a response arriving…) is over at `frame`.
    virtual bool isFinished(AgentMarkFrame frame) const = 0;
    // Draws the mark at `frame` in `box`, in `color`. With
    // motion off (Motion::policy()), the timer does not run: the mark draws
    // a still form that tells the state apart by its color alone.
    virtual void paint(QPainter &painter, const QRectF &box, const QColor &color, AgentMarkFrame frame) const = 0;
};
