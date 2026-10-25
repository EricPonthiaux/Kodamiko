// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>
#include <QString>

struct CMakePreset {
    QString name;
    QString configurePreset; // empty for a configure preset, or when not given
};

struct CMakePresetLists {
    QList<CMakePreset> configure;
    QList<CMakePreset> build;
    QList<CMakePreset> test;
};

namespace CMakePresets {

// The presets of `projectFolder`'s CMakePresets.json and CMakeUserPresets.json
// (the first file to name one wins) that a user can run on this system: not
// hidden, with a condition, their own or inherited, that holds here, and for a
// build or test preset, a configure preset that is listed too. Empty when
// neither file exists or parses.
CMakePresetLists forThisSystem(const QString &projectFolder);

} // namespace CMakePresets
