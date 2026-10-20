// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QRegularExpression>
#include <QString>

// The envelope and the ids of an explainer scene (sidecar's
// sciilo-board-v1.schema.json), shared by its reader and its writer.
namespace diagram::explainer {

inline const QString sceneFormat = QStringLiteral("sciilo-board");
constexpr int sceneVersion = 1;

// An element id both apps accept; `__` is reserved for sidecar's own ids.
inline bool isSafeId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:-]{0,79}$"));
    return pattern.match(id).hasMatch() && !id.startsWith(QLatin1String("__"));
}

} // namespace diagram::explainer
