#pragma once

#include "git/FilePatch.h"
#include "ui/Theme.h"

#include <Qsci/qsciscintilla.h>

#include <QHash>

// File shown by a DiffView.
struct DiffFile {
    QString path;   // as shown in its title bar
    QString status; // "modified", "added", "renamed from x.cpp"…
    FilePatch patch;
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
    // Words that differ between a removed line and the line added in its
    // place; nothing when they have too little in common for it to help.
    static std::pair<QList<Range>, QList<Range>> wordChanges(const QString &removed, const QString &added);

private:
    void render();
    void defineStyles();
    // Colors the code of file `file` as the editor would.
    void highlightSyntax(int file);
    // Style of the diff for a syntax color, defined on first use.
    int syntaxStyle(long foreground, bool bold, bool italic);

    Theme theme_;
    QList<Line> lines_;
    QList<int> fileStarts_;
    QStringList paths_;
    int numberWidth_ = 3;             // digits of the largest line number
    QHash<qint64, int> syntaxStyles_; // color, bold and italic → style
};
