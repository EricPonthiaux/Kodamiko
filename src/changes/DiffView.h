#pragma once

#include "git/FilePatch.h"
#include "ui/Theme.h"

#include <QHash>
#include <Qsci/qsciscintilla.h>

#include <functional>

class ScratchEditors;

// File shown by a DiffView.
struct DiffFile {
    QString path;   // as shown in its title bar
    QString status; // "modified", "added", "renamed from x.cpp"…
    FilePatch patch;
    bool isTooLarge = false; // listed without its patch, see FileDiff
};

// Diff without git's notation, for reading: a title bar per file with its
// added and removed line counts, a separator per block of changes ("⋯ lines
// 40–48 · function"), the lines themselves without "+" or "-", colored as in
// the editor, added ones on a green background and removed ones on a red
// one, the words that changed within a line brighter still, and in the
// margin their numbers before and after. Read only; the text can be selected
// and copied.
class DiffView : public QsciScintilla {
    Q_OBJECT

public:
    enum class LineKind {
        Spacer,
        FileHeader,
        HunkHeader,
        Context,
        Added,
        Removed,
        Note
    };

    // What a displayed line stands for in the files' patches. `hunk` is -1
    // on a file's title bar, `line` -1 on a block separator.
    struct Location {
        int file = -1;
        int hunk = -1;
        int line = -1;
    };

    // Characters of a line's text, from `start`.
    struct Range {
        int start = 0;
        int length = 0;
        bool operator==(const Range &) const = default;
    };

    struct Line {
        QString text;
        LineKind kind = LineKind::Context;
        Location location;
        int oldNumber = 0; // from 1; 0 when the line is not on that side
        int newNumber = 0;
        int emphasis = 0;    // title bars: characters of the file name, shown brighter
        int statsStart = -1; // title bars: where "+12 −3" begins
        // Added or removed lines facing their counterpart: the words that changed.
        QList<Range> changes;
    };

    // What a file's title bar says, for a header above the view.
    struct FileSummary {
        QString path;
        QString status;
        int added = 0;
        int removed = 0;
    };

    explicit DiffView(const Theme &theme, QWidget *parent = nullptr);

    void setFiles(const QList<DiffFile> &files);
    // Nothing to show but a message in gray (or nothing).
    void showMessage(const QString &message);

    Location locate(int line) const;
    // Line of a file's title bar.
    int fileStart(int file) const;

    const QList<FileSummary> &fileSummaries() const { return summaries_; }

    // The block separator of the block holding displayed line `line`; -1
    // outside a block.
    int hunkStart(int line) const;
    // Scrolls the next (or previous) block of changes to the top.
    void goToChange(bool forward);
    // Where displayed line `line` is in the viewport, and the lines' height.
    int lineTop(int line) const;
    int lineHeight() const;

    const QList<Line> &displayedLines() const { return lines_; }

    void applyTheme(const Theme &theme);

signals:
    // setFiles() or showMessage() changed what is shown.
    void contentChanged();

public:
    // The displayed lines for `files`.
    static QList<Line> layout(const QList<DiffFile> &files);
    // Words that differ between a removed line and the line added in its
    // place; nothing when they have too little in common for it to help.
    static std::pair<QList<Range>, QList<Range>> wordChanges(const QString &removed, const QString &added);

private:
    void render();
    // Markers and margin text of displayed line `line`; returns the style of its text.
    int decorateLine(int line);
    void styleLine(int line, int style);
    // "+12" in green, "−3" in red, on a file's title bar.
    void styleStats(const Line &title, long start, long length);
    // A line number right-justified for the margin; blank when `value` is 0.
    QString marginNumber(int value) const;
    void defineStyles();
    // Colors the code of every file as the editor would, within a budget.
    void highlightFiles();
    // Colors the code of file `file` with `scratches` if it fits in
    // `budget` bytes; returns the bytes colored.
    qsizetype highlightSyntax(int file, ScratchEditors &scratches, qsizetype budget);
    // Styles displayed line `line` from the scratch editor's styles of its
    // bytes, through `diffStyle`.
    void styleRuns(int line, QByteArrayView scratchStyles, const std::function<int(int)> &diffStyle);
    // Style of the diff for a syntax color, defined on first use.
    int syntaxStyle(long foreground, bool bold, bool italic);

    Theme theme_;
    QList<Line> lines_;
    QList<int> fileStarts_;
    QStringList paths_;
    QList<FileSummary> summaries_;
    int numberWidth_ = 3;             // digits of the largest line number
    QHash<qint64, int> syntaxStyles_; // color, bold and italic → style
};
