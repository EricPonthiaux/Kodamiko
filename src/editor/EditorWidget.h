#pragma once

#include "TextFile.h"
#include "diagnostics/Diagnostic.h"
#include "ui/Theme.h"

#include <QKeySequence>
#include <QList>
#include <QMenu>
#include <Qsci/qsciscintilla.h>

#include <functional>
#include <memory>
#include <utility>

class QAction;
class QPainter;

class ChangeGutter;
class SymbolOccurrences;
class ISyntaxHighlighter;

// Display settings shared by every editor (Preferences ▸ Editor).
// When the completion popup opens by itself; Ctrl+Space always opens it.
enum class AutoCompletion {
    Off,   // only on Ctrl+Space
    Calm,  // after a pause in typing, from the third letter of a name
    Eager, // as soon as a name or a member access is typed
};

struct EditorOptions {
    bool codeFolding = true;
    bool wrapLines = false;
    bool showWhitespace = false;
    AutoCompletion autoCompletion = AutoCompletion::Eager;
    // Enter inserts the selected suggestion; otherwise only Tab does, and
    // Enter always starts a new line.
    bool completionAcceptsEnter = true;

    bool operator==(const EditorOptions &) const = default;
};

// Editor for one file: QScintilla configured for code, with highlighting
// for the language detected from the file name.
class EditorWidget : public QsciScintilla {
    Q_OBJECT

public:
    // The indentation guide of the caret's block: its column and the lines
    // it runs along. Column 0: none.
    struct ActiveGuide {
        int column = 0;
        int firstLine = 0;
        int lastLine = 0;
        bool operator==(const ActiveGuide &) const = default;
    };

    explicit EditorWidget(const Theme &theme, QWidget *parent = nullptr);
    ~EditorWidget() override;

    bool load(const QString &path, QString *error);
    bool save(QString *error);

    // Rereads the file, keeping the cursor and scroll positions.
    // The operation can be undone (Ctrl+Z) like an ordinary edit.
    bool reloadFromDisk(QString *error);
    bool matchesDisk() const;
    // Replaces the text with its formatted version (the file's line ending
    // is kept), as a single edit that can be undone. The cursor and the
    // selection stay next to the same code. Returns false if nothing changed.
    bool applyFormattedText(QString formatted);

    // Read-only text with no associated file (e.g. a diff).
    void showReadOnlyText(const QString &text, const QString &languageId);

    // The document's UTF-8 bytes, without the copies and conversions of
    // text(): valid until the next change to the document.
    QByteArrayView utf8Bytes() const;
    // Bytes [start, end[ of the document, copied in one call: for a short
    // range, cheaper than utf8Bytes(), which may move the gap buffer.
    QByteArray utf8Range(int start, int end) const;

    QString filePath() const { return filePath_; }

    // LanguageRegistry identifier of the displayed text.
    QString languageId() const { return languageId_; }

    // Encoding and line ending of the file, kept when saving.
    TextFileFormat format() const { return format_; }

    // Converts every line ending of the text and uses `lineEnding` for new
    // lines; the conversion can be undone.
    void setLineEnding(LineEnding lineEnding);
    // Indentation inserted by Tab and auto-indent.
    void setIndentationStyle(bool useTabs, int width);
    // Vertical line after `columnLimit` characters, the line length the
    // formatter keeps to; 0 hides it.
    void showColumnLimit(int columnLimit);
    void setOptions(const EditorOptions &options);

    // Actions at the top of the context menu when the editor shows a file
    // (Claude's requests about the code). Not owned.
    void setFileActions(const QList<QAction *> &actions) { fileActions_ = actions; }

    // The context menu: QScintilla's, with the file actions on top.
    std::unique_ptr<QMenu> createContextMenu();

    const EditorOptions &options() const { return options_; }

    // Folds (or unfolds) the innermost block around the cursor.
    void foldBlock(bool fold);
    // Folds (or unfolds) every block of the text.
    void foldEverything(bool fold);

    // Key combinations QScintilla must leave to the application's shortcuts
    // (menu commands), in every editor. Without them, QScintilla takes the
    // keys it has a command for (Ctrl+- zooms out...).
    static void setReservedKeys(const QList<QKeySequence> &shortcuts);

    // Problems found in the text: a wavy underline, red for an error, amber
    // for a warning, and their messages when the mouse rests on them.
    void setDiagnostics(const Diagnostics &diagnostics);

    const Diagnostics &diagnostics() const { return diagnostics_; }

    // Text shown when the mouse rests where there is no problem (the
    // documentation of a setting...), from a byte position; empty: nothing.
    void setHoverHelp(std::function<QString(int)> help);

    // Marks of the lines that differ from the git index.
    ChangeGutter *changeGutter() const { return changeGutter_; }

    // The occurrences of the name at the cursor, highlighted.
    SymbolOccurrences *symbolOccurrences() const { return symbolOccurrences_; }

    void applyTheme(const Theme &theme);

    const Theme &theme() const { return theme_; }

signals:
    void languageChanged();
    // The line ending or the indentation settings changed.
    void formatChanged();
    // Ctrl+click on a word (the cursor is already placed there).
    void definitionRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void configureEditing();
    // Releases the reserved keys from QScintilla's commands, which keep
    // their other keys.
    void releaseReservedKeys();
    void applyFoldColors();
    // The open blocks' chevrons show only while the mouse is over the
    // margins; those of the folded blocks always.
    void showOpenFoldMarks(bool shown);
    // The caret moved: its block's indentation guide lights up and its line
    // number brightens.
    void followCaretLine();
    void paintActiveGuide(QPainter &painter);
    void paintCurrentLineNumber(QPainter &painter, const QRect &dirty);
    // The cursor got into a folded block (search, jump to a line): the block
    // opens.
    void revealCursorLine(int line);
    // After folding: a cursor on a hidden line goes to the end of the
    // visible header of its fold.
    void moveCursorOutOfFolds();
    void installHighlighter(const QString &languageId);
    void updateLineNumberMargin();
    void showHoverInfo(int position, const QPoint &point);
    // The mouse must rest a moment for the hover info (Scintilla's default:
    // never).
    void enableDwell();

    QString filePath_;
    QString languageId_;
    TextFileFormat format_;
    Theme theme_;
    std::unique_ptr<ISyntaxHighlighter> highlighter_;
    ChangeGutter *changeGutter_;
    SymbolOccurrences *symbolOccurrences_;
    EditorOptions options_;
    Diagnostics diagnostics_;
    bool openFoldMarksShown_ = false;
    // The line whose number paintEvent brightens.
    int numberedLine_ = -1;
    ActiveGuide activeGuide_;
    QList<QAction *> fileActions_;
    std::function<QString(int)> hoverHelp_;
    // QScintilla's keys for each of its commands, before releaseReservedKeys.
    QList<std::pair<int, int>> defaultCommandKeys_;
};
