// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "lsp/LspProtocol.h"

#include <QList>
#include <QString>

#include <functional>

// A call or type hierarchy, as the Hierarchy pane shows it: from its
// roots, the elements found one level at a time, when it is unfolded.
struct Hierarchy {
    // Up: the callers, or the supertypes; Down: the called, or the subtypes.
    enum class Direction {
        Up,
        Down
    };

    using ChildrenReply = std::function<void(const QList<LspProtocol::HierarchyItem> &children)>;
    // Asks for the elements under `parent` in `direction`; `reply` gets
    // them, later, or not at all if the server is gone.
    using Children =
        std::function<void(Direction direction, const LspProtocol::HierarchyItem &parent, const ChildrenReply &reply)>;

    QString title;    // "Calls of “total”"
    QString upName;   // "Callers"
    QString downName; // "Callees"
    QList<LspProtocol::HierarchyItem> roots;
    Children children;
};
