// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "agent/AgentMark.h"

// Claude's burst (see UiIcons' "claude"): eight branches, the long ones on
// the axes. While Claude works, each branch lights up in turn; while it
// waits for the user, the whole star breathes; when a response arrives, the
// long and the short branches flash in turn, then the star shines still.
class ClaudeMark final : public AgentMark {
public:
    std::optional<std::chrono::milliseconds> frameStep(AgentStatus status) const override;
    int loopFrames() const override;
    bool isFinished(AgentMarkFrame frame) const override;
    void paint(QPainter &painter, const QRectF &box, const QColor &color, AgentMarkFrame frame) const override;
};
