// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "SketchScene.h"

#include <QStringView>

// A small, typical diagram of each kind, for DiagramSketch.
namespace SketchScenes {

// The scene of the diagram family `kindId` ("flowchart", "sequence"… as
// DiagramKind::id); a flowchart's for an id it does not know.
SketchScene forKind(QStringView kindId);

} // namespace SketchScenes
