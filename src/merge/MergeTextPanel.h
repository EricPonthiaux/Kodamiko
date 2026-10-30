// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "MergeDocument.h"
#include "git/Integration.h"
#include "ui/Theme.h"

#include <QList>
#include <QWidget>

class MergeGutter;
class MergePane;

// The resolution of a conflicted text file: the current side, the result
// and the incoming side in three columns that face each other row for row
// and scroll together, an arrow at each conflict to take a side's lines
// into the result (both sides, after a second arrow). F7 and Shift+F7 go
// to the next and previous conflict; Ctrl+Z and Ctrl+Shift+Z undo and redo.
class MergeTextPanel : public QWidget {
    Q_OBJECT

public:
    enum class Direction {
        Previous,
        Next
    };

    // A conflicted file: `path` colors the code; `document`, not owned,
    // must outlive the panel.
    struct File {
        QString path;
        MergeDocument *document = nullptr;
        Integration::ConflictSides sides;
    };

    MergeTextPanel(const Theme &theme, const File &file, QWidget *parent);

    void goToConflict(Direction direction);
    void applyTheme(const Theme &theme);

signals:
    // The document changed (a side taken, an undo…).
    void documentChanged();

private:
    void layOut(const Integration::ConflictSides &sides);
    QWidget *createHeader(const QString &name, const QString &role);
    void connectPanes();
    void addShortcuts();
    void take(int segment, MergeDocument::Choice side);
    void refresh();
    void updateMarks();
    void alignWith(const MergePane &leader);
    QList<int> conflictSegments() const;

    MergeDocument &document_;
    MergePane *current_;
    MergePane *result_;
    MergePane *incoming_;
    MergeGutter *currentGutter_;
    MergeGutter *incomingGutter_;
    // The first row of each segment, the same in the three columns.
    QList<int> segmentRows_;
};
