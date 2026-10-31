// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "agent/AgentMark.h"

// The Claude agent's mark (see UiIcons' "claude"): a ring of eight dots, the
// large ones on the axes, a neutral motif rather than Anthropic's logo. While
// Claude works, each dot lights up in turn; while it waits for the user, the
// whole ring breathes; when a response arrives, the large and the small dots
// flash in turn, then the ring shines still.
class ClaudeMark final : public AgentMark {
public:
    std::optional<std::chrono::milliseconds> frameStep(AgentStatus status) const override;
    int loopFrames() const override;
    bool isFinished(AgentMarkFrame frame) const override;
    void paint(QPainter &painter, const QRectF &box, const QColor &color, AgentMarkFrame frame) const override;
};
