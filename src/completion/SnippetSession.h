#pragma once

#include "Snippet.h"

#include <QObject>

class EditorWidget;

// The tab stops of a snippet just inserted, as in IntelliJ's live
// templates: boxes show them, Tab selects the next one and Shift+Tab the
// previous, and what is typed in one is copied to its other occurrences
// ("for (int i = 0; i < n; ++i)"). It ends on the last stop ($0, else the
// snippet's end), on Escape, on undo, or when the cursor leaves the
// snippet.
class SnippetSession : public QObject {
    Q_OBJECT

public:
    explicit SnippetSession(EditorWidget *editor);

    // Follows `snippet`, just inserted at byte `start`, and selects its
    // first stop. Nothing to follow without a numbered stop.
    void start(const ExpandedSnippet &snippet, int start);
    bool isActive() const { return !stops_.isEmpty(); }
    // Selects the next stop; on the last one, the session ends.
    void next();
    void previous();
    void stop();
    // The index of the selected stop ($1 → 1), -1 when inactive. For tests.
    int currentIndex() const;

private:
    struct Range {
        int start = 0; // bytes
        int end = 0;
    };
    struct Stop {
        int index = 0;
        QList<Range> ranges;
    };

    void onModified(int position, int modificationType, int length);
    void onCursorMoved();
    // Copies the edited occurrence of the current stop to the others,
    // then redraws the boxes.
    void synchronize();
    void select();
    void paint();
    void clearBoxes();

    EditorWidget *editor_;
    QList<Stop> stops_; // the last one is $0
    int current_ = 0;
    bool synchronizing_ = false;
    bool synchronizePending_ = false;
};
