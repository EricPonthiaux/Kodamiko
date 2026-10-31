// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QColor>
#include <QString>

#include <ScintillaEditBase.h>

#include <memory>

class QImage;
class QMenu;
class QPoint;

// Scintilla's colors are 0xBBGGRR; with an alpha, 0xAABBGGRR.
sptr_t scintillaColor(const QColor &color);
sptr_t scintillaColorWithAlpha(const QColor &color);

// A Scintilla editor for the editor's views: Scintilla's own Qt widget,
// plus what it leaves to its host. Text and positions in characters
// (Scintilla counts UTF-8 bytes), Qt-style signals, and the behaviors a code
// editor expects: auto-indent, brace matching, folding by the margin, and
// macOS's keys. Everything else is Scintilla's messages, through send().
//
// Lines and indexes are 0-based; an index counts characters in its line.
// The document is UTF-8. setText(), insert(), insertAt() and append() write
// even when the view is read-only, which only stops the user.
class ScintillaView : public ScintillaEditBase {
    Q_OBJECT

public:
    explicit ScintillaView(QWidget *parent = nullptr);

    // The document's bytes, contiguous, without a copy: valid until the
    // next change (SCI_GETCHARACTERPOINTER).
    [[nodiscard]] const char *characterPointer() const;

    QString text() const;
    // Line `line`'s text, its end of line included.
    QString text(int line) const;
    // The text between positions `start` and `end`.
    QString text(int start, int end) const;
    // Bytes [start, end[ of the document, copied in one call: for a short
    // range, cheaper than characterPointer(), which may move the gap buffer.
    QByteArray utf8Range(int start, int end) const;
    void setText(const QString &text);
    // In bytes.
    int length() const;
    int lines() const;
    // Inserts `text` at the caret, which stays before it.
    void insert(const QString &text);
    void insertAt(const QString &text, int line, int index);
    void append(const QString &text);

    QString selectedText() const;
    bool hasSelectedText() const;
    void removeSelectedText();
    void replaceSelectedText(const QString &text);
    void selectAll(bool select = true);

    void getCursorPosition(int *line, int *index) const;
    void setCursorPosition(int line, int index);
    // The selection's anchor-independent bounds: from (lineFrom, indexFrom)
    // to (lineTo, indexTo); all -1 without a selection.
    void getSelection(int *lineFrom, int *indexFrom, int *lineTo, int *indexTo) const;
    void setSelection(int lineFrom, int indexFrom, int lineTo, int indexTo);

    int positionFromLineIndex(int line, int index) const;
    void lineIndexFromPosition(int position, int *line, int *index) const;
    // The line at `point` in the viewport; -1 below the last one.
    int lineAt(const QPoint &point) const;
    int firstVisibleLine() const;
    void setFirstVisibleLine(int line);
    void ensureLineVisible(int line);
    // The word around (line, index), or an empty string.
    QString wordAtLineIndex(int line, int index) const;
    // In columns.
    int indentation(int line) const;
    void setIndentation(int line, int indentation);

    bool isModified() const;
    void setModified(bool modified);
    bool isReadOnly() const;
    void setReadOnly(bool readOnly);
    void undo();
    void redo();
    void beginUndoAction();
    void endUndoAction();
    void cut();
    void copy();
    void paste();
    void zoomIn();
    void zoomOut();
    void zoomTo(int size);

    bool isUndoAvailable() const;
    bool isRedoAvailable() const;
    // In columns.
    int tabWidth() const;
    bool indentationsUseTabs() const;
    void setCaretLineVisible(bool isVisible);
    // Styles the whole text again (SCI_COLOURISE).
    void recolor();

    // Undo, Redo (when editable), Cut, Copy, Paste, Delete and Select All,
    // for the caller to show.
    std::unique_ptr<QMenu> createStandardContextMenu();

    // Marker `marker` draws `symbol` (SC_MARK_…).
    void markerDefine(int symbol, int marker);
    // Marker `marker` draws `image`, at its device pixel ratio.
    void markerDefine(const QImage &image, int marker);
    void setMarkerBackgroundColor(const QColor &color, int marker);
    void setMarkerForegroundColor(const QColor &color, int marker);
    // A handle for SCI_MARKERLINEFROMHANDLE, or -1 when it failed.
    int markerAdd(int line, int marker);
    void markerDelete(int line, int marker);
    // Every marker `marker`, or every marker when -1.
    void markerDeleteAll(int marker = -1);

    // `type` is an SC_MARGIN_… value.
    void setMarginType(int margin, int type);
    void setMarginWidth(int margin, int widthPx);
    // As wide as `sample` in the line numbers' style.
    void setMarginWidth(int margin, const QString &sample);
    int marginWidth(int margin) const;
    void setMarginMarkerMask(int margin, int mask);
    void setMarginSensitivity(int margin, bool isSensitive);
    void setMarginText(int line, const QString &text, int style);
    void clearMarginText();

    // Copies the previous line's indentation on a new line.
    void setAutoIndent(bool isEnabled);
    // Highlights the brace before or after the caret, and its match.
    void setBraceMatching(bool isEnabled);
    // A fold margin at `margin`, whose clicks fold and unfold the blocks; 0
    // wide when off. The lexer or highlighter sets the fold levels.
    void setFolding(bool isEnabled, int margin);

signals:
    // The text was inserted into or deleted from.
    void textChanged();
    // Lines were added or removed.
    void linesChanged();
    // The caret moved to `line`, at column `column` (tabs expanded).
    void cursorPositionChanged(int line, int column);
    void selectionChanged();
    // The document left or came back to its save point.
    void modificationChanged(bool isModified);
    // A margin other than the fold margin was clicked at `line`.
    void marginLineClicked(int margin, int line, Qt::KeyboardModifiers modifiers);
    // The mouse rested at `point` in the viewport, near `position` (-1 when
    // over no text), for SCI_SETMOUSEDWELLTIME.
    void dwellStarted(int position, QPoint point);
    void dwellEnded();
    // A click on a hotspot (SCI_STYLESETHOTSPOT) at `position` was released.
    void hotspotReleased(int position);

private:
    void onModified(Scintilla::ModificationFlags type, Scintilla::Position linesAdded);
    void onUpdateUi(Scintilla::Update updated);
    void onMarginClicked(Scintilla::Position position, Scintilla::KeyMod modifiers, int margin);
    void onCharAdded(int character);
    void onNotification(const Scintilla::NotificationData *notification);
    void indentNewLine();
    void matchBraces();
    void bindSystemKeys();

    bool isAutoIndentEnabled_ = false;
    bool isBraceMatchingEnabled_ = false;
    int foldMargin_ = -1;
    sptr_t lastCaret_ = -1;
    sptr_t lastAnchor_ = -1;
};
