// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QJsonObject>
#include <QString>

namespace diagram {
class Board;
}

namespace diagram::explainer {

// The explainer scene of `board`, as sidecar's JSON.stringify(document,
// null, 2) writes it, keys in sidecar's order, so that a board saved by
// either app reads (and diffs) the same. `meta` is what the source last
// read gave: the canvas, the steps and the elements' order, kept as is.
QString writeScene(const Board &board, const QJsonObject &meta);

} // namespace diagram::explainer
