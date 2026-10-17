#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// The kinds of diagram Duet can show, for the menus that create one (blank
// or drawn by Claude). Only those the diagram editor supports are listed:
// none when Duet is built without it.
struct DiagramKind {
    QString id;        // "flowchart"
    QString title;     // for menus: "Flowchart"
    QString extension; // without the dot: "mmd"
    QByteArray blank;  // the content of a new, empty diagram
};

namespace DiagramKinds {

QList<DiagramKind> available();
// nullopt-like: an empty id when `id` is not available.
DiagramKind find(const QString &id);

// `name` with the kind's extension added, unless it already has it.
QString withExtension(const QString &name, const DiagramKind &kind);

} // namespace DiagramKinds
