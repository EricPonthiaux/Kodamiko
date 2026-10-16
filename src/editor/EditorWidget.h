#pragma once

#include "TextFile.h"
#include "syntax/Theme.h"

#include <Qsci/qsciscintilla.h>

#include <QKeySequence>
#include <QList>

#include <memory>
#include <utility>

class ChangeGutter;
class ISyntaxHighlighter;

// Display settings shared by every editor (Preferences ▸ Editor).
struct EditorOptions {
    bool codeFolding = true;
    bool wrapLines = false;
    bool showWhitespace = false;
    // The completion popup opens while typing; Ctrl+Space always opens it.
    bool autoCompletion = true;

    bool operator==(const EditorOptions &) const = default;
};

// Editor for one file: QScintilla configured for code, with highlighting
// for the language detected from the file name.
class EditorWidget : public QsciScintilla {
    Q_OBJECT

public:
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
    void setOptions(const EditorOptions &options);
    const EditorOptions &options() const { return options_; }

    // Folds (or unfolds) the innermost block around the cursor.
    void foldBlock(bool fold);
    // Folds (or unfolds) every block of the text.
    void foldEverything(bool fold);

    // Key combinations QScintilla must leave to the application's shortcuts
    // (menu commands), in every editor. Without them, QScintilla takes the
    // keys it has a command for (Ctrl+- zooms out...).
    static void setReservedKeys(const QList<QKeySequence> &shortcuts);

    // Marks of the lines that differ from the git index.
    ChangeGutter *changeGutter() const { return changeGutter_; }
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

private:
    void configureEditing();
    // Releases the reserved keys from QScintilla's commands, which keep
    // their other keys.
    void releaseReservedKeys();
    void applyFoldColors();
    // The cursor got into a folded block (search, jump to a line): the block
    // opens.
    void revealCursorLine(int line);
    // After folding: a cursor on a hidden line goes to the end of the
    // visible header of its fold.
    void moveCursorOutOfFolds();
    void installHighlighter(const QString &languageId);
    void updateLineNumberMargin();

    QString filePath_;
    QString languageId_;
    TextFileFormat format_;
    Theme theme_;
    std::unique_ptr<ISyntaxHighlighter> highlighter_;
    ChangeGutter *changeGutter_;
    EditorOptions options_;
    // QScintilla's keys for each of its commands, before releaseReservedKeys.
    QList<std::pair<int, int>> defaultCommandKeys_;
};
