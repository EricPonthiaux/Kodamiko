// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>

class EditorWidget;

// Checks Claude Code's configuration files in the editors that show them
// (see ClaudeLint): once opened, then shortly after each change. In a
// settings file, the mouse resting on a key shows its documentation.
class ClaudeLinter : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // Follows `editor` if its file is one Claude Code reads.
    void attach(EditorWidget *editor);
};
