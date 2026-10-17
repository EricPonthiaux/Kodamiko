#pragma once

#include <QSizeF>
#include <QString>
#include <QStringList>

// Layout with Graphviz, as sidecar does it (with Graphviz compiled to
// WebAssembly; here the C library): the same DOT is written and the same
// JSON read back, so that diagrams are laid out alike.
namespace diagram {

class Board;

namespace graphviz {

struct ArrangeOptions {
    QString rankdir = QStringLiteral("TB");
    // 'dot' layered (flowcharts, classes, states); 'twopi' radial around a
    // root (mind maps).
    QString engine = QStringLiteral("dot");
    QString root;
    bool clusters = false;
    QString splines;
    bool inferClusterMembership = false;
    bool stackDisconnected = false;
    // Tries the other of LR and TB, kept if the view frames it 15 % larger.
    bool adaptiveDirection = false;
    // The view the diagram is framed in, for adaptiveDirection.
    QSizeF viewSize;
};

// Lays the board out: positions, cluster boxes, edge curves and label
// places. One step of history. False, with `error`, if Graphviz failed.
bool autoArrange(Board &board, const ArrangeOptions &options, QString *error = nullptr);

// Places the shapes `ids` (new among placed ones) without moving the others:
// each goes where a full layout puts it relative to a placed neighbor, or
// beside the diagram when it has none, then is nudged off any overlap. Not
// a step of history.
void placeNewcomers(Board &board, const QStringList &ids, const ArrangeOptions &options);

// Asks Graphviz for the edge routes alone, every node pinned where it is:
// a link finds its way around a shape the moment that shape is dropped,
// without reorganizing a hand-made layout. Not a step of history.
bool routeLinks(Board &board);

// Runs a layout engine on DOT and returns Graphviz's JSON output.
QByteArray layout(const QByteArray &dot, const char *engine, QString *error = nullptr);

// Font metrics compensation (graphviz-metrics.js): Graphviz measures text
// with Arial's metrics; the board draws with its own font. Font sizes in the
// DOT are scaled by the ratio, so that Graphviz reserves the room the text
// really takes.
qreal fontScale();
QString dotFontSize(qreal px, qreal scale);
QString dotEscape(const QString &text);
QString dotHtmlEscape(const QString &text);

} // namespace graphviz
} // namespace diagram
