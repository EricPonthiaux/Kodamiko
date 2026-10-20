#pragma once

#include "DiffLayout.h"
#include "ui/Theme.h"

#include <Qsci/qsciscintilla.h>

#include <optional>

// Diff without git's notation, for reading: a title bar per file with its
// added and removed line counts, a separator per block of changes ("⋯ lines
// 40–48 · function"), the lines themselves without "+" or "-", colored as in
// the editor, added ones on a green background and removed ones on a red
// one, the words that changed within a line brighter still, and in the
// margin their numbers before and after. Read only; the text can be selected
// and copied. F7 and Shift+F7 go to the next and previous block, Alt+Down
// and Alt+Up to the next and previous file.
class DiffView : public QsciScintilla {
    Q_OBJECT

public:
    // What a file's title bar says, for a header above the view.
    struct FileSummary {
        QString path;
        QString status;
        int added = 0;
        int removed = 0;
    };

    enum class Direction {
        Previous,
        Next
    };

    explicit DiffView(const Theme &theme, QWidget *parent = nullptr);

    void setFiles(const QList<DiffFile> &files);
    // Nothing to show but a message in gray (or nothing).
    void showMessage(const QString &message);

    DiffLocation locate(int line) const;
    // Line of a file's title bar.
    int fileStart(int file) const;

    const QList<FileSummary> &fileSummaries() const { return summaries_; }

    // The block separator of the block holding displayed line `line`; -1
    // outside a block.
    int hunkStart(int line) const;
    // Scrolls the next (or previous) block of changes to the top and puts
    // the cursor on its separator. Next after the cursor when it is in view,
    // otherwise after the top of the view.
    void goToChange(Direction direction);
    // Same for the next (or previous) file's title bar.
    void goToFile(Direction direction);
    // The line, from 0, of the file's new version that displayed line `line`
    // stands for: the line itself, or the next line of the new version for a
    // removed line, a block separator or a title bar.
    int newFileLine(int line) const;
    // Where displayed line `line` is in the viewport, and the lines' height.
    int lineTop(int line) const;
    int lineHeight() const;

    const QList<DiffLine> &displayedLines() const { return lines_; }

    void applyTheme(const Theme &theme);

signals:
    // setFiles() or showMessage() changed what is shown.
    void contentChanged();

private:
    void addShortcuts();
    // The first displayed line of `kind` after (or before) the cursor or the
    // top of the view, see goToChange().
    std::optional<int> nextLineOfKind(DiffLineKind kind, Direction direction) const;
    // Scrolls displayed line `line` to the top with the cursor on it.
    void showAtTop(int line);
    void render();
    // Markers and margin text of displayed line `line`; returns the style of its text.
    int decorateLine(int line);
    void styleLine(int line, int style);
    // "+12" in green, "−3" in red, on a file's title bar.
    void styleStats(const DiffLine &title, long start, long length);
    // A line number right-justified for the margin; blank when `value` is 0.
    QString marginNumber(int value) const;

    Theme theme_;
    QList<DiffLine> lines_;
    QList<int> fileStarts_;
    QStringList paths_;
    QList<FileSummary> summaries_;
    int numberWidth_ = 3; // digits of the largest line number
};
