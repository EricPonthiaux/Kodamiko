#pragma once

#include "git/FilePatch.h"
#include "ui/Theme.h"

#include <Qsci/qsciscintilla.h>

// File shown by a DiffView.
struct DiffFile {
    QString path;   // as shown in its title bar
    QString status; // "modified", "added", "renamed from x.cpp"…
    FilePatch patch;
};

// Diff without git's notation, for reading: a title bar per file, a
// separator per block of changes ("⋯ lines 40–48 · function"), the lines
// themselves without "+" or "-", added ones on a green background and
// removed ones on a red one, and in the margin their numbers before and
// after. Read only; the text can be selected and copied.
class DiffView : public QsciScintilla {
    Q_OBJECT

public:
    enum class LineKind { Spacer, FileHeader, HunkHeader, Context, Added, Removed, Note };

    // What a displayed line stands for in the files' patches. `hunk` is -1
    // on a file's title bar, `line` -1 on a block separator.
    struct Location {
        int file = -1;
        int hunk = -1;
        int line = -1;
    };

    struct Line {
        QString text;
        LineKind kind = LineKind::Context;
        Location location;
        int oldNumber = 0; // from 1; 0 when the line is not on that side
        int newNumber = 0;
        int emphasis = 0; // title bars: characters of the file name, shown brighter
    };

    explicit DiffView(const Theme &theme, QWidget *parent = nullptr);

    void setFiles(const QList<DiffFile> &files);
    // Nothing to show but a message in gray (or nothing).
    void showMessage(const QString &message);

    Location locate(int line) const;
    // Line of a file's title bar.
    int fileStart(int file) const;
    const QList<Line> &lines() const { return lines_; }

    void applyTheme(const Theme &theme);

    // The displayed lines for `files`.
    static QList<Line> layout(const QList<DiffFile> &files);

private:
    void render();
    void defineStyles();

    Theme theme_;
    QList<Line> lines_;
    QList<int> fileStarts_;
    int numberWidth_ = 3; // digits of the largest line number
};
