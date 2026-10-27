// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/ConflictMarkers.h"
#include "ui/Theme.h"

#include <QWidget>

class QToolButton;

// The ways to resolve one merge conflict, as links next to its first
// marker in the editor (see MergeConflicts): Accept Current, Accept
// Incoming, Accept Both.
class ConflictActionBar : public QWidget {
    Q_OBJECT

public:
    ConflictActionBar(const Theme &theme, QWidget *parent);

    void applyTheme(const Theme &theme);

    // The link that chooses `resolution`.
    QToolButton *button(ConflictMarkers::Resolution resolution) const;

signals:
    void resolutionChosen(ConflictMarkers::Resolution resolution);

private:
    QToolButton *addLink(const QString &text, ConflictMarkers::Resolution resolution);

    QToolButton *current_ = nullptr;
    QToolButton *incoming_ = nullptr;
    QToolButton *both_ = nullptr;
};
