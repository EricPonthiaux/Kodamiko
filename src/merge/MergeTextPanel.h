// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ConflictState.h"
#include "MergeDocument.h"
#include "git/Integration.h"
#include "ui/Theme.h"

#include <QList>
#include <QWidget>

class GripSplitter;
class MergeFocusFrame;
class MergeGutter;
class MergeOverview;
class MergePane;
class MergeProposalBar;
class QTimer;

// The resolution of a conflicted text file: the current side, the result
// and the incoming side in three columns that face each other row for row
// and scroll together, an arrow at each conflict to take a side's lines
// into the result (both sides, after a second arrow), Take Yours or
// Take Incoming for every conflict left. The result is typed in as in an
// editor. One conflict is the one shown: framed across the columns, the
// others dimmed (MergeFocusFrame), outlined in the overview of the whole
// file at the right (MergeOverview). F7 and Shift+F7 show the next and
// previous conflict, Alt+F7 the next one still open; taking a side moves on
// to it. Ctrl+Z and Ctrl+Shift+Z undo and redo, the typing as the sides
// taken. The agent's proposals show in the result, in the agent's color,
// and in a bar under the columns to accept or reject them.
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
        QString agentName; // who makes the proposals (MergeDocument::propose())
    };

    MergeTextPanel(const Theme &theme, const File &file, QWidget *parent);

    void goToConflict(Direction direction);
    // `index` indexes conflictStates().
    void goToConflict(int index);
    void goToNextOpenConflict();
    // Each conflict of the file, in order, and the index of the one shown
    // (-1 for none).
    QList<ConflictState> conflictStates() const;
    int currentConflict() const;
    // Resolves the conflicts that need no one to decide; returns how many.
    int resolveSimpleConflicts();
    // Shows the proposals the document was just given, from the first.
    void showProposals();
    void applyTheme(const Theme &theme);

signals:
    // The document changed (a side taken, an undo…).
    void documentChanged();
    // The conflicts' states, or the one shown, changed.
    void conflictsChanged();
    // Taking a side resolved the conflict `index`; the view moves on to
    // `next`, the next one still open, -1 for none.
    void conflictResolved(int index, int next);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void layOut(const Integration::ConflictSides &sides);
    QWidget *createHeader(const QString &name, const QString &role);
    QWidget *createSideHeader(const QString &name, const QString &role, MergeDocument::Choice side);
    void connectProposalBar();
    void connectPanes();
    void goToProposal(Direction direction);
    void updateProposalBar();
    void scrollTo(int segment);
    // Makes `segment` the conflict shown, and brings it into view.
    void showConflict(int segment);
    // After a side taken or a proposal accepted at `segment`, open before:
    // on to the next conflict still open.
    void moveOnFrom(int segment);
    int nextOpenSegment(int after) const;
    ConflictState stateOf(int segment) const;
    void updateFocus();
    void updateOverview();
    QList<int> proposalSegments() const;
    void addShortcuts();
    void take(int segment, MergeDocument::Choice side);
    // The user typed in the result: the document follows.
    void onResultEdited();
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
    MergeOverview *overview_;
    MergeFocusFrame *focus_;
    GripSplitter *columns_ = nullptr;
    int headerHeight_ = 0;
    MergeProposalBar *proposalBar_;
    // Take Yours and Take Incoming: shown while a conflict is open.
    QList<QWidget *> takeAllButtons_;
    QTimer *typingPause_;
    QString agentName_;
    // The segment whose proposal the bar shows.
    int proposalSegment_ = -1;
    // Set while the columns are rewritten: their changes are not typing.
    bool isShowing_ = false;
    // The first line of the result changed since the document last
    // followed, -1 for none.
    int firstEditedLine_ = -1;
    // The first row of each segment, the same in the three columns, and
    // the rows of them all.
    QList<int> segmentRows_;
    int totalRows_ = 0;
    // The conflict shown, a segment; -1 for none.
    int currentSegment_ = -1;
};
