// SPDX-License-Identifier: GPL-3.0-only

#include "TerminalWidget.h"

#include "IPty.h"
#include "VtScreen.h"
#include "settings/Preferences.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMenu>
#include <QPaintEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {

constexpr int initialRows = 24;
constexpr int initialColumns = 80;

// Terminal-specific copy/paste: Ctrl+C and Ctrl+V must remain available to
// the program (Ctrl+C interrupts Claude).
bool isCopyShortcut(const QKeyEvent *event)
{
#ifdef Q_OS_MACOS
    return event->key() == Qt::Key_C && event->modifiers() == Qt::ControlModifier; // Cmd+C
#else
    return event->key() == Qt::Key_C && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier);
#endif
}

bool isPasteShortcut(const QKeyEvent *event)
{
    if (event->key() == Qt::Key_Insert && event->modifiers() == Qt::ShiftModifier)
        return true;
#ifdef Q_OS_MACOS
    return event->key() == Qt::Key_V && event->modifiers() == Qt::ControlModifier; // Cmd+V
#else
    return event->key() == Qt::Key_V && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier);
#endif
}

// Reading order: row then column. QPoint(x = column, y = row).
bool comesBefore(const QPoint &a, const QPoint &b)
{
    return a.y() < b.y() || (a.y() == b.y() && a.x() < b.x());
}

int fontIndex(bool bold, bool italic, bool underline, bool strike)
{
    return (bold ? 1 : 0) | (italic ? 2 : 0) | (underline ? 4 : 0) | (strike ? 8 : 0);
}

int fontIndex(const VTermScreenCell &cell)
{
    return fontIndex(cell.attrs.bold, cell.attrs.italic, cell.attrs.underline != VTERM_UNDERLINE_OFF,
                     cell.attrs.strike);
}

bool isDecorated(int fontIndex)
{
    return fontIndex & (4 | 8);
}

// A printable ASCII character alone in its cell: always in the main font,
// so it can be drawn within a run.
bool isRunCharacter(const VTermScreenCell &cell)
{
    return cell.chars[0] >= 0x20 && cell.chars[0] < 0x7f && cell.chars[1] == 0 && cell.width == 1;
}

bool isContinuation(const VTermScreenCell &cell)
{
    return cell.chars[0] == uint32_t(-1);
}

// Where a row's text goes: the baseline and the width of a cell.
struct RowGeometry {
    int baseline = 0;
    int cellWidth = 0;
};

// Draws the text of one row: runs of ASCII characters sharing a font and a
// color in one drawText() call each, the other characters one by one.
class RowTextPainter {
public:
    // `runText` is a buffer reused across paints to avoid allocating.
    RowTextPainter(QPainter &painter, const std::array<QFont, 16> &fonts, QString &runText, RowGeometry geometry)
        : painter_(&painter)
        , fonts_(&fonts)
        , runText_(&runText)
        , geometry_(geometry)
    {
        runText_->clear();
        painter_->setPen(QColor(currentPen_));
    }

    // A plain space fits in any run whose font draws nothing on it.
    void addPlainSpace()
    {
        if (!runText_->isEmpty() && !isDecorated(runFont_))
            *runText_ += QLatin1Char(' ');
    }

    void addRunCharacter(int column, int font, QRgb pen, char character)
    {
        const bool extendsRun = !runText_->isEmpty() && font == runFont_ && pen == runPen_;
        if (!extendsRun) {
            flush();
            runColumn_ = column;
            runFont_ = font;
            runPen_ = pen;
        }
        *runText_ += QLatin1Char(character);
    }

    // A character that may come from a fallback font with another advance:
    // drawn alone, at its cell.
    void drawAlone(int column, int font, QRgb pen, const VTermScreenCell &cell)
    {
        flush();
        int length = 0;
        while (length < VTERM_MAX_CHARS_PER_CELL && cell.chars[length] != 0)
            ++length;
        select(font, pen);
        painter_->drawText(column * geometry_.cellWidth, geometry_.baseline,
                           QString::fromUcs4(reinterpret_cast<const char32_t *>(cell.chars), length));
    }

    void flush()
    {
        if (!isDecorated(runFont_)) {
            while (runText_->endsWith(QLatin1Char(' ')))
                runText_->chop(1);
        }
        if (!runText_->isEmpty()) {
            select(runFont_, runPen_);
            painter_->drawText(runColumn_ * geometry_.cellWidth, geometry_.baseline, *runText_);
        }
        runText_->clear();
    }

private:
    void select(int font, QRgb pen)
    {
        if (font != currentFont_) {
            painter_->setFont((*fonts_)[static_cast<size_t>(font)]);
            currentFont_ = font;
        }
        if (pen != currentPen_) {
            painter_->setPen(QColor(pen));
            currentPen_ = pen;
        }
    }

    QPainter *painter_;
    const std::array<QFont, 16> *fonts_;
    QString *runText_;
    RowGeometry geometry_;
    int currentFont_ = -1;
    QRgb currentPen_ = 0;
    int runColumn_ = 0;
    int runFont_ = 0;
    QRgb runPen_ = 0;
};

} // namespace

TerminalWidget::TerminalWidget(QWidget *parent)
    : QAbstractScrollArea(parent)
    , screen_(std::make_unique<VtScreen>(initialRows, initialColumns))
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);

    connect(screen_.get(), &VtScreen::screenChanged, this, &TerminalWidget::onScreenChanged);
    connect(screen_.get(), &VtScreen::titleChanged, this, &TerminalWidget::titleChanged);
    connect(screen_.get(), &VtScreen::bell, this, [] { QApplication::beep(); });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (updatingScrollBar_)
            return;
        scrollOffset_ = verticalScrollBar()->maximum() - value;
        viewport()->update();
    });

    applyTheme(Theme::dark());
}

TerminalWidget::~TerminalWidget() = default;

bool TerminalWidget::start(const ProcessSpec &spec)
{
    pty_ = createPty();
    connect(pty_.get(), &IPty::dataReceived, screen_.get(), &VtScreen::feed);
    connect(pty_.get(), &IPty::dataReceived, this, &TerminalWidget::outputReceived);
    connect(screen_.get(), &VtScreen::outputReady, pty_.get(), &IPty::write);
    connect(pty_.get(), &IPty::finished, this, &TerminalWidget::onProcessFinished);

    running_ = pty_->start(spec, QSize(screen_->columns(), screen_->rows()));
    if (!running_) {
        errorString_ = pty_->errorString();
        screen_->feed(QStringLiteral("\x1b[31m%1\x1b[0m\r\n").arg(errorString_).toUtf8());
    }
    return running_;
}

void TerminalWidget::interrupt()
{
    if (running_)
        pty_->write(QByteArrayLiteral("\x03"));
}

void TerminalWidget::terminate()
{
    if (!running_)
        return;
    running_ = false;
    pty_.reset();
    screen_->feed(QByteArrayLiteral("\r\n\x1b[2m[Process stopped]\x1b[0m\r\n"));
    emit finished(-1);
}

void TerminalWidget::applyTheme(const Theme &theme)
{
    baseFont_ = theme.font;
    foreground_ = theme.foreground;
    background_ = theme.background;
    screen_->setDefaultColors(foreground_, background_);
    applyFont();
}

void TerminalWidget::applyFont()
{
    QFont font = baseFont_;
    font.setStyleHint(QFont::TypeWriter);
    font.setKerning(false);
    if (font.pointSizeF() > 0)
        font.setPointSizeF(std::clamp(font.pointSizeF() + zoom_, qreal(Preferences::minimumFontSize),
                                      qreal(Preferences::maximumFontSize)));

    const QFontMetricsF metrics(font);
    cellSize_ = QSize(static_cast<int>(std::ceil(metrics.horizontalAdvance(QLatin1Char('M')))),
                      static_cast<int>(std::ceil(metrics.lineSpacing())));
    ascent_ = static_cast<int>(std::ceil(metrics.ascent()));

    for (int index = 0; index < static_cast<int>(fonts_.size()); ++index) {
        QFont variant = font;
        variant.setBold(index & 1);
        variant.setItalic(index & 2);
        variant.setUnderline(index & 4);
        variant.setStrikeOut(index & 8);
        const qreal advance = QFontMetricsF(variant).horizontalAdvance(QLatin1Char('M'));
        variant.setLetterSpacing(QFont::AbsoluteSpacing, cellSize_.width() - advance);
        fonts_[static_cast<size_t>(index)] = variant;
    }

    updateGridSize();
    viewport()->update();
}

void TerminalWidget::onProcessFinished(int exitCode)
{
    running_ = false;
    screen_->feed(QStringLiteral("\r\n\x1b[2m[Process exited, code %1]\x1b[0m\r\n").arg(exitCode).toUtf8());
    emit finished(exitCode);
}

void TerminalWidget::onScreenChanged()
{
    const QRect damage = screen_->takeDamage();
    updateScrollBar();
    if (scrollOffset_ != 0 || damage.isEmpty()) {
        viewport()->update();
        return;
    }
    // One extra column on each side: wide and italic glyphs overhang.
    const QRect cells = damage.adjusted(-1, 0, 1, 0);
    viewport()->update(QRect(cells.x() * cellSize_.width(), cells.y() * cellSize_.height(),
                             cells.width() * cellSize_.width(), cells.height() * cellSize_.height()));
}

void TerminalWidget::updateGridSize()
{
    // A hidden tab has a placeholder size: its output would wrap at a dozen
    // columns. The grid follows the size it is shown at.
    if (!isVisible())
        return;
    const int columns = std::max(2, viewport()->width() / cellSize_.width());
    const int rows = std::max(1, viewport()->height() / cellSize_.height());
    if (columns == screen_->columns() && rows == screen_->rows())
        return;
    screen_->resize(rows, columns);
    if (pty_)
        pty_->resize(QSize(columns, rows));
}

void TerminalWidget::updateScrollBar()
{
    // Not a QSignalBlocker: QAbstractScrollArea shows the bar ("as needed")
    // on its rangeChanged() signal.
    QScrollBar *bar = verticalScrollBar();
    updatingScrollBar_ = true;
    bar->setRange(0, screen_->scrollbackSize());
    bar->setPageStep(screen_->rows());
    bar->setValue(screen_->scrollbackSize() - scrollOffset_);
    updatingScrollBar_ = false;
}

void TerminalWidget::scrollToBottom()
{
    scrollOffset_ = 0;
    updateScrollBar();
    viewport()->update();
}

bool TerminalWidget::isModeShortcut(const QKeyEvent &event)
{
    if (event.modifiers() != Qt::ControlModifier)
        return false;
    if (event.key() >= Qt::Key_1 && event.key() <= Qt::Key_4)
        return true;
    // Keys 1 to 4 of the top row, whatever the layout.
#ifdef Q_OS_MACOS
    const quint32 code = event.nativeVirtualKey(); // kVK_ANSI_1..4
    return code >= 0x12 && code <= 0x15;
#elif defined(Q_OS_WIN)
    const quint32 code = event.nativeScanCode();
    return code >= 0x02 && code <= 0x05;
#else
    const quint32 code = event.nativeScanCode(); // XKB keycode (evdev + 8)
    return code >= 10 && code <= 13;
#endif
}

bool TerminalWidget::isApplicationShortcut(const QKeyEvent &event)
{
    if (event.key() == Qt::Key_F11 || isModeShortcut(event))
        return true;
    if (isCopyShortcut(&event) || isPasteShortcut(&event))
        return false; // the terminal's own
    const Qt::KeyboardModifiers modifiers = event.modifiers() & ~Qt::KeypadModifier;
#ifdef Q_OS_MACOS
    // Cmd (Qt's Control) belongs to the application, as in Terminal.app.
    return modifiers & Qt::ControlModifier;
#else
    if (!(modifiers & Qt::ControlModifier))
        return false;
    // AltGr is Ctrl+Alt on Windows: a character typed with it is text.
    if ((modifiers & Qt::AltModifier) && !event.text().isEmpty() && event.text().at(0).isPrint())
        return false;
    return modifiers & (Qt::ShiftModifier | Qt::AltModifier);
#endif
}

bool TerminalWidget::event(QEvent *event)
{
    // The terminal wants the keys programs use: plain Ctrl+letter (Ctrl+C,
    // Ctrl+R…), Alt+letter, Escape, the function keys. Combinations they do
    // not use go to the application's shortcuts (see isApplicationShortcut).
    if (event->type() == QEvent::ShortcutOverride) {
        if (isApplicationShortcut(*static_cast<QKeyEvent *>(event)))
            return QAbstractScrollArea::event(event);
        event->accept();
        return true;
    }
    return QAbstractScrollArea::event(event);
}

bool TerminalWidget::focusNextPrevChild(bool)
{
    return false; // Tab and Shift+Tab go to the program (completion, Claude's modes)
}

void TerminalWidget::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    scheduleGridUpdate();
}

void TerminalWidget::showEvent(QShowEvent *event)
{
    QAbstractScrollArea::showEvent(event);
    scheduleGridUpdate();
}

void TerminalWidget::scheduleGridUpdate()
{
    // Once the layout settles: a page being shown passes through sizes that
    // would cut its lines.
    if (gridUpdatePending_)
        return;
    gridUpdatePending_ = true;
    QTimer::singleShot(0, this, [this] {
        gridUpdatePending_ = false;
        updateGridSize();
    });
}

void TerminalWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(viewport());
    const QRect area = event->rect();
    painter.fillRect(area, background_);

    const int firstRow = std::max(0, area.top() / cellSize_.height());
    const int lastRow = std::min(screen_->rows() - 1, area.bottom() / cellSize_.height());
    for (int row = firstRow; row <= lastRow; ++row)
        paintRow(painter, row);
    if (scrollOffset_ == 0)
        paintCursor(painter);
}

// Backgrounds are filled in runs of one color, then the text is drawn in
// runs of ASCII characters sharing a font and a color: one drawText() per
// run instead of one per cell. Other characters (box drawing, CJK, emoji)
// may come from a fallback font with another advance: they are drawn one
// by one, at their cell.
void TerminalWidget::paintRow(QPainter &painter, int row)
{
    const int contentRow = row - scrollOffset_;
    screen_->readRow(contentRow, rowCells_);
    computeRowColors(contentRow);
    const int y = row * cellSize_.height();
    paintRowBackgrounds(painter, y);

    RowTextPainter text(painter, fonts_, runText_, {.baseline = y + ascent_, .cellWidth = cellSize_.width()});
    const int columns = static_cast<int>(rowCells_.size());
    for (int column = 0; column < columns; ++column) {
        const VTermScreenCell &cell = rowCells_[static_cast<size_t>(column)];
        if (isContinuation(cell))
            continue;
        const int font = fontIndex(cell);
        const QRgb pen = rowForegrounds_[static_cast<size_t>(column)];
        const bool blank = cell.chars[0] == 0 || cell.chars[0] == ' ';
        if (blank && !isDecorated(font))
            text.addPlainSpace();
        else if (blank)
            text.addRunCharacter(column, font, pen, ' ');
        else if (isRunCharacter(cell))
            text.addRunCharacter(column, font, pen, static_cast<char>(cell.chars[0]));
        else
            text.drawAlone(column, font, pen, cell);
    }
    text.flush();
}

// Colors of each cell of rowCells_, the selection reversed.
void TerminalWidget::computeRowColors(int contentRow)
{
    rowForegrounds_.resize(rowCells_.size());
    rowBackgrounds_.resize(rowCells_.size());
    for (size_t index = 0; index < rowCells_.size(); ++index) {
        if (index > 0 && isContinuation(rowCells_[index])) {
            // Second half of a wide character: the colors of the first.
            rowForegrounds_[index] = rowForegrounds_[index - 1];
            rowBackgrounds_[index] = rowBackgrounds_[index - 1];
            continue;
        }
        screen_->colorsOf(rowCells_[index], rowForegrounds_[index], rowBackgrounds_[index]);
        if (isSelected(contentRow, static_cast<int>(index)))
            std::swap(rowForegrounds_[index], rowBackgrounds_[index]);
    }
}

// Backgrounds in runs of one color; the default one is already there.
void TerminalWidget::paintRowBackgrounds(QPainter &painter, int y) const
{
    const int columns = static_cast<int>(rowBackgrounds_.size());
    const int cellWidth = cellSize_.width();
    const QRgb defaultBackground = background_.rgb();
    int runStart = 0;
    for (int column = 1; column <= columns; ++column) {
        const QRgb color = rowBackgrounds_[static_cast<size_t>(runStart)];
        if (column < columns && rowBackgrounds_[static_cast<size_t>(column)] == color)
            continue;
        if (color != defaultBackground)
            painter.fillRect(runStart * cellWidth, y, (column - runStart) * cellWidth, cellSize_.height(),
                             QColor(color));
        runStart = column;
    }
}

const QFont &TerminalWidget::fontFor(bool bold, bool italic, bool underline, bool strike) const
{
    return fonts_[static_cast<size_t>(fontIndex(bold, italic, underline, strike))];
}

void TerminalWidget::paintCell(QPainter &painter, const TerminalCell &cell, const QPoint &topLeft) const
{
    const QRect area(topLeft, QSize(cellSize_.width() * cell.width, cellSize_.height()));
    if (cell.background != background_)
        painter.fillRect(area, cell.background);
    if (cell.text.isEmpty() || cell.text == QLatin1String(" "))
        return;

    painter.setFont(fontFor(cell.bold, cell.italic, cell.underline, cell.strike));
    painter.setPen(cell.foreground);
    painter.drawText(topLeft.x(), topLeft.y() + ascent_, cell.text);
}

void TerminalWidget::paintCursor(QPainter &painter) const
{
    if (!screen_->isCursorVisible())
        return;
    const QPoint cursor = screen_->cursorPosition();
    const QRect area(cursor.x() * cellSize_.width(), cursor.y() * cellSize_.height(), cellSize_.width(),
                     cellSize_.height());
    if (!hasFocus()) {
        painter.setPen(foreground_);
        painter.drawRect(area.adjusted(0, 0, -1, -1));
        return;
    }
    TerminalCell cell = screen_->cell(cursor.y(), cursor.x());
    cell.width = 1;
    cell.foreground = background_;
    cell.background = foreground_;
    paintCell(painter, cell, area.topLeft());
}

void TerminalWidget::keyPressEvent(QKeyEvent *event)
{
    if (isCopyShortcut(event)) {
        copySelection(QClipboard::Clipboard);
        return;
    }
    if (isPasteShortcut(event)) {
        pasteClipboard();
        return;
    }
    if (screen_->sendKey(event->key(), event->modifiers(), event->text())) {
        scrollToBottom();
        event->accept();
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

// Characters composed by an input method (dead keys, IME).
void TerminalWidget::inputMethodEvent(QInputMethodEvent *event)
{
    if (!event->commitString().isEmpty())
        screen_->sendKey(0, Qt::NoModifier, event->commitString());
    event->accept();
}

void TerminalWidget::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom, as in the editors: one point per notch.
        const int delta = event->angleDelta().y();
        const qreal size = baseFont_.pointSizeF();
        if (delta != 0 && size > 0) {
            const int zoom =
                std::clamp(zoom_ + (delta > 0 ? 1 : -1), Preferences::minimumFontSize - static_cast<int>(size),
                           Preferences::maximumFontSize - static_cast<int>(size));
            if (zoom != zoom_) {
                zoom_ = zoom;
                applyFont();
            }
        }
        event->accept();
        return;
    }
    if (screen_->isMouseReporting() && !(event->modifiers() & Qt::ShiftModifier)) {
        // The program scrolls itself; Shift+wheel keeps Duet's history.
        wheelRemainder_ += event->angleDelta().y();
        const int notches = wheelRemainder_ / 120;
        wheelRemainder_ -= notches * 120;
        const QPoint cell = cellAt(event->position().toPoint());
        for (int i = 0; i < std::abs(notches); ++i)
            screen_->sendWheel(cell.y() + scrollOffset_, cell.x(), notches > 0, event->modifiers());
        event->accept();
        return;
    }
    const int lines = event->angleDelta().y() / 40;
    scrollOffset_ = std::clamp(scrollOffset_ + lines, 0, screen_->scrollbackSize());
    updateScrollBar();
    viewport()->update();
}

QPoint TerminalWidget::cellAt(const QPoint &widgetPosition) const
{
    const int column = std::clamp(widgetPosition.x() / cellSize_.width(), 0, screen_->columns() - 1);
    const int row = std::clamp(widgetPosition.y() / cellSize_.height(), 0, screen_->rows() - 1);
    return {column, row - scrollOffset_};
}

void TerminalWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    if (event->button() == Qt::MiddleButton) {
        // Paste the primary selection, as in X11 terminals.
        const QString text = QApplication::clipboard()->text(QClipboard::Selection);
        if (!text.isEmpty())
            screen_->paste(text);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    selecting_ = true;
    selectionAnchor_ = cellAt(event->position().toPoint());
    selectionEnd_ = selectionAnchor_;
    viewport()->update();
}

void TerminalWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!selecting_)
        return;
    selectionEnd_ = cellAt(event->position().toPoint());
    viewport()->update();
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !selecting_)
        return;
    selecting_ = false;
    selectionEnd_ = cellAt(event->position().toPoint());
    if (selectionEnd_ == selectionAnchor_) {
        selectionAnchor_ = QPoint(-1, 0); // single click: no selection
        selectionEnd_ = selectionAnchor_;
    } else if (QApplication::clipboard()->supportsSelection()) {
        copySelection(QClipboard::Selection);
    }
    viewport()->update();
}

// Copy and paste with the mouse: the terminal's keyboard shortcuts differ
// from the editor's (see isCopyShortcut).
void TerminalWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *copy = menu.addAction(tr("Copy"), this, [this] { copySelection(QClipboard::Clipboard); });
    copy->setEnabled(!selectedText().isEmpty());
    QAction *paste = menu.addAction(tr("Paste"), this, &TerminalWidget::pasteClipboard);
    paste->setEnabled(!QApplication::clipboard()->text().isEmpty());
    menu.exec(event->globalPos());
}

void TerminalWidget::focusInEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusInEvent(event);
    viewport()->update();
}

void TerminalWidget::focusOutEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusOutEvent(event);
    viewport()->update();
}

bool TerminalWidget::isSelected(int contentRow, int column) const
{
    if (selectionAnchor_.x() < 0 || selectionAnchor_ == selectionEnd_)
        return false;
    const QPoint first = comesBefore(selectionAnchor_, selectionEnd_) ? selectionAnchor_ : selectionEnd_;
    const QPoint last = comesBefore(selectionAnchor_, selectionEnd_) ? selectionEnd_ : selectionAnchor_;
    const QPoint position(column, contentRow);
    return !comesBefore(position, first) && !comesBefore(last, position);
}

QString TerminalWidget::selectedText() const
{
    if (selectionAnchor_.x() < 0 || selectionAnchor_ == selectionEnd_)
        return {};
    const QPoint first = comesBefore(selectionAnchor_, selectionEnd_) ? selectionAnchor_ : selectionEnd_;
    const QPoint last = comesBefore(selectionAnchor_, selectionEnd_) ? selectionEnd_ : selectionAnchor_;

    QStringList lines;
    for (int row = first.y(); row <= last.y(); ++row) {
        const QString line = screen_->lineText(row);
        const int from = row == first.y() ? first.x() : 0;
        const int to = row == last.y() ? last.x() + 1 : line.size();
        lines.append(line.mid(from, std::max(0, to - from)));
    }
    return lines.join(QLatin1Char('\n'));
}

void TerminalWidget::copySelection(QClipboard::Mode mode) const
{
    const QString text = selectedText();
    if (!text.isEmpty())
        QApplication::clipboard()->setText(text, mode);
}

void TerminalWidget::pasteClipboard()
{
    paste(QApplication::clipboard()->text());
}

void TerminalWidget::paste(const QString &text)
{
    if (text.isEmpty())
        return;
    screen_->paste(text);
    scrollToBottom();
}

void TerminalWidget::pressEnter()
{
    if (!screen_)
        return;
    screen_->sendKey(Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    scrollToBottom();
}

QStringList TerminalWidget::lastLines(int count) const
{
    QStringList lines;
    if (!screen_)
        return lines;
    int last = screen_->rows() - 1;
    while (last >= -screen_->scrollbackSize() && screen_->lineText(last).trimmed().isEmpty())
        --last;
    for (int row = std::max(-screen_->scrollbackSize(), last - count + 1); row <= last; ++row) {
        QString line = screen_->lineText(row);
        while (!line.isEmpty() && line.back().isSpace())
            line.chop(1);
        lines.append(line);
    }
    return lines;
}
