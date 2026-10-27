// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/ConflictMarkers.h"
#include "ui/Theme.h"

#include <QList>
#include <QObject>
#include <QTimer>

#include <optional>

class ConflictActionBar;
class EditorWidget;
class ScrollBarMarks;

// The conflicts a merge or a rebase left in the file (see ConflictMarkers),
// shown in the editor: the current side's lines on a green background, the
// incoming side's on a blue one, and their markers marked along the scroll
// bar. Next to each conflict's first marker, links resolve it (Accept
// Current, Accept Incoming, Accept Both) as one edit that Ctrl+Z undoes.
// The conflicts follow the text as it is typed; a read-only editor shows
// them without the links.
class MergeConflicts : public QObject {
    Q_OBJECT

public:
    enum class Direction {
        Next,
        Previous,
    };

    // Shows the conflicts of `editor`, which owns it.
    MergeConflicts(EditorWidget *editor, Theme theme);

    void applyTheme(const Theme &theme);

    // The conflicts of the text, in order.
    const QList<ConflictMarkers::Conflict> &conflicts() const { return conflicts_; }

    // The conflict that holds byte `position`, markers included.
    std::optional<int> conflictAt(int position) const;

    // Replaces conflict `index` by the lines `resolution` keeps, and puts
    // the cursor at their start.
    void resolve(int index, ConflictMarkers::Resolution resolution);

    // Moves the cursor to the first marker of the next or previous
    // conflict, wrapping around. Returns false if there is none.
    bool goTo(Direction direction);

    // The links of conflict `index`; null in a read-only editor. For tests.
    ConflictActionBar *actionBar(int index) const;

    // Reads the conflicts now rather than after the delay. For tests.
    void refreshNow();

private:
    void show();
    void defineMarkers();
    void markLines();
    // The links of each conflict, at the end of its first marker's line;
    // hidden while that line is out of view.
    void placeActionBars();
    void updateActionBarCount();
    void markScrollBar();

    EditorWidget *editor_;
    ScrollBarMarks *marks_;
    Theme theme_;
    QTimer update_;
    QList<ConflictMarkers::Conflict> conflicts_;
    // Owned by the editor's viewport; one per conflict.
    QList<ConflictActionBar *> actionBars_;
};
