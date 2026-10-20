// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>

// The diagram families, one directory each (families/<name>/), ported from
// sidecar's shape-board families and their fences. Each returns its family
// and the codecs that read and write it, in detection order.
namespace diagram {

struct Family;
class Codec;

namespace families {

const Family &flowchart();
QList<const Codec *> flowchartCodecs();

const Family &sequence();
QList<const Codec *> sequenceCodecs();

const Family &classes();
QList<const Codec *> classCodecs();

const Family &states();
QList<const Codec *> stateCodecs();

const Family &dataModel();
QList<const Codec *> dataModelCodecs();

const Family &c4();
QList<const Codec *> c4Codecs();

const Family &mindmap();
QList<const Codec *> mindmapCodecs();

const Family &journey();
QList<const Codec *> journeyCodecs();

const Family &productVision();
QList<const Codec *> productVisionCodecs();

const Family &explainer();
QList<const Codec *> explainerCodecs();

} // namespace families
} // namespace diagram
