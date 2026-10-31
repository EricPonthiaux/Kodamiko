// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "OptionKey.h"

#include <QColor>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QString>

#include <vterm.h>

#include <deque>
#include <vector>

// One cell of the terminal grid, ready to draw.
struct TerminalCell {
    QString text; // empty for a blank cell
    QColor foreground;
    QColor background;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    int width = 1;               // 2 for a wide character (CJK, emoji)
    bool isContinuation = false; // second half of a wide character
};

// Rows of the screen that moved as a block (a scroll): `area`, in cells,
// holds the rows before and after the move; `rows` < 0 moves them up.
struct ScreenScroll {
    QRect area;
    int rows = 0;
};

// What changed on the screen since the last VtScreen::takeChanges(). A view
// that shows the screen applies `scrolls` to its picture, in order, then
// repaints `damage`, which is already where the scrolls left it.
struct ScreenChanges {
    std::vector<ScreenScroll> scrolls;
    QRegion damage;      // cells (x = column, y = row)
    int pushedLines = 0; // lines that went into the history (< 0: came back from it)
};

// Screen of a VT/xterm terminal, emulated by libvterm.
//
//   program output --feed()--> libvterm --> cell grid (cell())
//   keys, pastes   --sendKey()/paste()--> libvterm --> outputReady()
//
// Lines that scroll off the top of the screen are kept in a history;
// cell() accesses them with negative row numbers.
class VtScreen : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(VtScreen)

public:
    VtScreen(int rows, int columns, QObject *parent = nullptr);
    ~VtScreen() override;

    void feed(const QByteArray &data);
    void resize(int rows, int columns);
    void setDefaultColors(const QColor &foreground, const QColor &background);

    int rows() const { return rows_; }

    int columns() const { return columns_; }

    int scrollbackSize() const { return static_cast<int>(scrollback_.size()); }

    // `row` in [-scrollbackSize(), rows()[; 0 = first row of the screen.
    TerminalCell cell(int row, int column) const;
    QString lineText(int row) const;

    // Drawing without allocations: the raw cells of a row (columns() of
    // them, same numbering as cell()) and their colors, reverse video
    // applied.
    void readRow(int row, std::vector<VTermScreenCell> &cells) const;
    void colorsOf(const VTermScreenCell &cell, QRgb &foreground, QRgb &background) const;

    ScreenChanges takeChanges();

    QPoint cursorPosition() const { return cursor_; } // x = column, y = row

    bool isCursorVisible() const { return cursorVisible_; }

    bool isAlternateScreen() const { return alternateScreen_; }

    // The program asked for mouse events (full-screen programs that scroll
    // themselves, such as Claude Code's).
    bool isMouseReporting() const { return mouseReporting_; }

    // Key press. `key`: Qt::Key, `text`: produced text (QKeyEvent::text);
    // `optionKey` says whether macOS's Option composed it or acts as Meta.
    // Returns false if the key has no terminal equivalent.
    bool sendKey(int key, Qt::KeyboardModifiers modifiers, const QString &text, OptionKey optionKey);
    void paste(const QString &text);
    // A wheel notch at a screen cell, in the encoding the program asked for.
    void sendWheel(int row, int column, bool up, Qt::KeyboardModifiers modifiers);
    // The left button pressed, moved or released at a screen cell (x =
    // column, y = row), in the encoding the program asked for. libvterm
    // sends a move only while the program tracks drags or motion.
    void sendLeftButtonPress(QPoint cell, Qt::KeyboardModifiers modifiers);
    void sendMouseMove(QPoint cell, Qt::KeyboardModifiers modifiers);
    void sendLeftButtonRelease(QPoint cell, Qt::KeyboardModifiers modifiers);

signals:
    void outputReady(const QByteArray &data); // to be written to the program
    void screenChanged();
    void titleChanged(const QString &title);
    void bell();

private:
    static int onDamage(VTermRect rect, void *user);
    static int onMoveRect(VTermRect destination, VTermRect source, void *user);
    static int onMoveCursor(VTermPos position, VTermPos oldPosition, int visible, void *user);
    static int onSetTermProp(VTermProp property, VTermValue *value, void *user);
    static int onBell(void *user);
    static int onPushLine(int columns, const VTermScreenCell *cells, void *user);
    static int onPopLine(int columns, VTermScreenCell *cells, void *user);
    static void onOutput(const char *bytes, size_t length, void *user);

    TerminalCell toTerminalCell(const VTermScreenCell &cell) const;
    QColor toQColor(VTermColor color, bool isForeground) const;
    QRgb toRgb(VTermColor color, bool isForeground) const;
    void addDamage(const QRect &cells);
    bool addScroll(const QRect &destination, const QRect &source);

    VTerm *terminal_ = nullptr;
    VTermScreen *screen_ = nullptr;
    VTermScreenCallbacks callbacks_{};
    int rows_;
    int columns_;
    QPoint cursor_;
    bool cursorVisible_ = true;
    bool alternateScreen_ = false;
    bool mouseReporting_ = false;
    QColor defaultForeground_ = Qt::lightGray;
    QColor defaultBackground_ = Qt::black;
    QString titleBuffer_;
    ScreenChanges changes_;
    bool changed_ = false; // something to signal at the end of feed()
    // Lines without their trailing blank cells.
    std::deque<std::vector<VTermScreenCell>> scrollback_;
};
