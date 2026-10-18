#pragma once

#include <QString>

#include <optional>

namespace diagram {
struct Model;
}

namespace diagram::explainer {

// Reads an explainer scene into `model`: its shapes and links keyed by
// their element ids, and in `model.meta` the canvas, the steps and the
// elements' source order. A scene that breaks the contract is refused as a
// whole: `model` is then left empty and the returned message says why, in
// sidecar's words. Returns nothing when the scene was read.
[[nodiscard]] std::optional<QString> readScene(const QString &source, Model &model);

} // namespace diagram::explainer
