#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// A diagram file: its source in a notation (Mermaid, PlantUML, a JSON
// scene) and, for the notations that know nothing of coordinates, the
// layout kept by Duet in comments at the end of the file:
//
//     %% duet {"key":"A","x":40,"y":40}
//     %% duet {"link":"A>B","n":0,"c":[…],"lp":[…]}
//
// As in sidecar, the source is the truth of the meaning and the layout that
// of the arrangement: an element keeps its place, size and color by its
// key, an element without one (written by hand, or by Claude) is laid out.
namespace diagram {

class Board;
class Codec;

namespace document {

struct Loaded {
    const Codec *codec = nullptr;
    // Nothing has a place (or the family is always laid out): the whole
    // diagram is laid out.
    bool needsArrange = false;
    // Shapes without a place among placed ones (added by hand or by Claude):
    // they are placed beside their neighbors, the rest keeps its place.
    QStringList unplaced;
    // Links without a curve: their routes are computed.
    bool needsRouting = false;
    QStringList warnings;
};

// Reads a file's content into `board`. False, with `error`, if the file is
// not a diagram Duet reads.
bool load(Board &board, const QString &path, const QByteArray &content, Loaded &loaded, QString *error);
// The file's content for the board, in the notation of `codec`.
QByteArray save(const Board &board, const Codec &codec);
// The source alone, without the layout comments.
QString source(const Board &board, const Codec &codec);
// Replaces the board's content with `source` in the same notation, keeping
// the layout by key; `laidOut` tells whether new elements need a layout.
bool applySource(Board &board, const Codec &codec, const QString &source, Loaded &loaded, QString *error);
// A new file of a family, in its default notation (empty if unknown).
QByteArray blank(const QString &familyId);
// The file extension of a family's files ("mmd").
QString extension(const QString &familyId);

// Problems of a diagram file, for whoever wrote it (Claude): unreadable
// source, statements not understood. Empty when the file is fine.
QStringList problems(const QString &path, const QByteArray &content);

} // namespace document
} // namespace diagram
