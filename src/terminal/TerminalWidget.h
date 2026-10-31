// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "GlyphCache.h"
#include "OptionKey.h"
#include "ProcessSpec.h"

#include <QAbstractScrollArea>
#include <QClipboard>
#include <QFont>
#include <QHash>
#include <QTimer>

#include <vterm.h>

#include <array>
#include <memory>
#include <optional>
#include <vector>

class IPty;
class QRegion;
class VtScreen;
struct Theme;
struct TerminalCell;
struct ScreenScroll;

// Graphical terminal: connects a PTY (the program) to a VtScreen (the
// emulated screen) and draws the cell grid. The scroll bar browses the
// history of lines that scrolled off the screen.
class QKeyEvent;

class TerminalWidget : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    bool start(const ProcessSpec &spec);

    QString errorString() const { return errorString_; }

    bool isRunning() const { return running_; }

    // A program other than the one started holds the terminal: a shell runs
    // a command. Always false on Windows.
    bool isBusy() const;

    // Ctrl+C, as typed: the program is asked to stop.
    void interrupt();
    // Ends the program at once (the terminal hangs up).
    void terminate();

    void applyTheme(const Theme &theme);
    // Types `text` as a paste (bracketed if the program asked for it).
    void paste(const QString &text);
    // Presses Enter, as typed.
    void pressEnter();
    // The last `count` lines shown (scrollback included), without the
    // blank lines at the end.
    QStringList lastLines(int count) const;

    // True for Ctrl+1/2/3/4 (mode switch), which the terminal leaves to the
    // application. Recognized by the physical key: on an AZERTY keyboard,
    // Ctrl+2 arrives as Ctrl+é.
    static bool isModeShortcut(const QKeyEvent &event);
    // Keys the terminal leaves to the application's shortcuts: F11,
    // Ctrl+1/2/3/4, and the combinations terminal programs do not use,
    // Ctrl+Shift+… and Ctrl+Alt+… (Cmd+… on macOS), except the terminal's
    // copy and paste.
    static bool isApplicationShortcut(const QKeyEvent &event);
    // What macOS's Option key does in every terminal (Compose until set).
    static void setOptionKey(OptionKey optionKey);

signals:
    void titleChanged(const QString &title);
    // The program's raw output, escapes included.
    void outputReceived(const QByteArray &data);
    // -1 after terminate().
    void finished(int exitCode);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool focusNextPrevChild(bool next) override;

private:
    // Columns of a row to paint, both included.
    struct ColumnSpan {
        int first = 0;
        int last = 0;
    };

    void onProcessFinished(int exitCode);
    void onScreenChanged();
    void copyScrolledRows(const std::vector<ScreenScroll> &scrolls);
    void followHistory(int pushedLines);
    void setScrollOffset(int offset);
    void shiftSelection(int rows);
    void updateSelectionRows(int contentRow, int otherContentRow);
    void updateGridSize();
    void scheduleGridUpdate();
    // Rebuilds the fonts and the cell size from the theme font and zoom_.
    void applyFont();
    void updateScrollBar();
    void scrollToBottom();

    std::optional<ColumnSpan> columnsToPaint(const QRegion &area, int row) const;
    void paintRow(QPainter &painter, int row, ColumnSpan columns);
    void computeRowColors(int contentRow);
    void paintRowBackgrounds(QPainter &painter, int y, ColumnSpan columns) const;
    bool fitsCellGrid(const VTermScreenCell &cell, int font);
    void paintCell(QPainter &painter, const TerminalCell &cell, const QPoint &topLeft) const;
    void paintCursor(QPainter &painter, const QRegion &area);
    const QFont &fontFor(bool bold, bool italic, bool underline, bool strike) const;
    // Viewport rectangle of a rectangle of screen cells.
    QRect pixelsOf(const QRect &cells) const;

    // Position (column, content row) under a widget point; content rows are
    // negative in the history.
    QPoint cellAt(const QPoint &widgetPosition) const;
    // The screen cell under a widget point, for the program's mouse events.
    QPoint screenCellAt(const QPoint &widgetPosition) const;
    bool isSelected(int contentRow, int column) const;
    QString selectedText() const;
    void copySelection(QClipboard::Mode mode) const;
    void clearSelection();
    void pasteClipboard();

    std::unique_ptr<IPty> pty_;
    std::unique_ptr<VtScreen> screen_;
    QString errorString_;
    bool running_ = false;

    // Every combination of bold, italic, underline and strike-out, with a
    // letter spacing that puts each character on the cell grid, so a run
    // of cells is drawn in one call (see fontFor()).
    std::array<QFont, 16> fonts_;
    // Per font of fonts_, whether a character joins runs (see fitsCellGrid()).
    std::array<QHash<char32_t, bool>, 16> fitsGrid_;
    GlyphCache glyphs_;
    QFont baseFont_;
    int zoom_ = 0;           // points added to the theme font by Ctrl+wheel
    int wheelRemainder_ = 0; // eighths of a degree not yet sent as a notch
    QSize cellSize_{8, 16};
    int ascent_ = 12;
    QColor foreground_;
    QColor background_;

    int scrollOffset_ = 0;           // history lines shown above the screen
    bool updatingScrollBar_ = false; // the bar follows scrollOffset_, not the reverse
    bool selecting_ = false;
    bool reportingClick_ = false; // the left button went to the program, not to the selection
    bool gridUpdatePending_ = false;
    QPoint selectionAnchor_{-1, 0};
    QPoint selectionEnd_{-1, 0};
    // Where the cursor was last drawn, in viewport coordinates: a copy of
    // scrolled rows moves its picture along.
    QRect paintedCursor_;
    // The program learns the new size once a window drag settles: each
    // resize makes it redraw its whole screen.
    QTimer ptyResizeTimer_;

    // Reused by paintRow() to avoid allocating on every paint.
    std::vector<VTermScreenCell> rowCells_;
    std::vector<QRgb> rowForegrounds_;
    std::vector<QRgb> rowBackgrounds_;
    QString runText_;
};
