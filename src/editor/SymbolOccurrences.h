#pragma once

#include "ui/Theme.h"

#include <QList>
#include <QObject>
#include <QTimer>

#include <optional>

class EditorWidget;
class ScrollBarMarks;

// Every occurrence of the name at the cursor, highlighted in the editor and
// marked along its scroll bar, so that a variable can be followed through
// the file. Found at once as the same whole word in the text; a language
// server, when one runs, then tells the occurrences of the very symbol
// (showServerOccurrences), which leave out a homonym in another scope, a
// comment or a keyword.
class SymbolOccurrences : public QObject {
    Q_OBJECT

public:
    // A range of the document, in bytes: [start, end[.
    struct Range {
        int start = 0;
        int end = 0;
        bool operator==(const Range &) const = default;
    };

    // Highlights in `editor`, which owns it.
    SymbolOccurrences(EditorWidget *editor, Theme theme);

    void applyTheme(const Theme &theme);

    // The highlighted occurrences, in document order.
    const QList<Range> &occurrences() const { return occurrences_; }

    // The occurrences a language server found for the name the cursor was
    // on at `position`; ignored if the cursor or the text has moved on since
    // nameSettled(position). Empty: the name is no symbol (a keyword…).
    void showServerOccurrences(int position, const QList<Range> &ranges);

    // Looks for the name at the cursor now rather than after the delay.
    // For tests.
    void refreshNow();

    // Most occurrences highlighted: beyond, a common word in a large file
    // would make every caret move slow.
    static constexpr int maximumOccurrences = 5000;

signals:
    // The cursor stays on a name at `position`, its occurrences shown from
    // the text: a language server may tell the exact ones.
    void nameSettled(int position);

private:
    // The name at the cursor: the word around it, or a selection that is
    // one word; nullopt elsewhere.
    std::optional<Range> nameAtCursor() const;
    QList<Range> wholeWordMatches(const QByteArray &word) const;
    void show(const QList<Range> &ranges);
    void clear();
    void styleIndicator();

    EditorWidget *editor_;
    ScrollBarMarks *marks_;
    Theme theme_;
    QTimer settle_;
    QList<Range> occurrences_;
    // Where the cursor settled on a name; -1 once it moved or the text
    // changed.
    int settledAt_ = -1;
};
