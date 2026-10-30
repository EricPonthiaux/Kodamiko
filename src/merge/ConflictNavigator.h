// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ConflictState.h"
#include "ui/Theme.h"

#include <QList>
#include <QWidget>

class ConflictSteps;
class QLabel;
class QToolButton;

// Where the merge view stands among the conflicts of the file shown: the
// arrows to the previous and next one around "Conflict 2 of 3", a dot per
// conflict in its state's color, the one shown ringed, and a button to the
// next conflict still open, which counts them.
class ConflictNavigator : public QWidget {
    Q_OBJECT

public:
    ConflictNavigator(const Theme &theme, QWidget *parent);

    // `current` indexes `states`; -1, or no states, for no conflict.
    void setConflicts(const QList<ConflictState> &states, int current);
    void applyTheme(const Theme &theme);

signals:
    void previousRequested();
    void nextRequested();
    void conflictRequested(int index);
    void nextOpenRequested();

private:
    QToolButton *previous_;
    QLabel *where_;
    QToolButton *next_;
    ConflictSteps *steps_;
    QToolButton *nextOpen_;
};
