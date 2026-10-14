#include "TerminalWidget.h"

#include "IPty.h"
#include "VtScreen.h"
#include "syntax/Theme.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>

#include <cmath>

namespace {

constexpr int initialRows = 24;
constexpr int initialColumns = 80;

// Copier/coller propres au terminal : Ctrl+C et Ctrl+V doivent rester
// disponibles pour le programme (Ctrl+C interrompt Claude).
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

// Ordre de lecture : ligne puis colonne. QPoint(x = colonne, y = ligne).
bool comesBefore(const QPoint &a, const QPoint &b)
{
    return a.y() < b.y() || (a.y() == b.y() && a.x() < b.x());
}

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
    connect(screen_.get(), &VtScreen::outputReady, pty_.get(), &IPty::write);
    connect(pty_.get(), &IPty::finished, this, &TerminalWidget::onProcessFinished);

    running_ = pty_->start(spec, QSize(screen_->columns(), screen_->rows()));
    if (!running_) {
        errorString_ = pty_->errorString();
        screen_->feed(QStringLiteral("\x1b[31m%1\x1b[0m\r\n").arg(errorString_).toUtf8());
    }
    return running_;
}

void TerminalWidget::applyTheme(const Theme &theme)
{
    font_ = theme.font;
    font_.setStyleHint(QFont::TypeWriter);
    font_.setKerning(false);
    boldFont_ = font_;
    boldFont_.setBold(true);
    italicFont_ = font_;
    italicFont_.setItalic(true);

    const QFontMetricsF metrics(font_);
    cellSize_ = QSize(static_cast<int>(std::ceil(metrics.horizontalAdvance(QLatin1Char('M')))),
                      static_cast<int>(std::ceil(metrics.lineSpacing())));
    ascent_ = static_cast<int>(std::ceil(metrics.ascent()));

    foreground_ = theme.foreground;
    background_ = theme.background;
    screen_->setDefaultColors(foreground_, background_);
    updateGridSize();
    viewport()->update();
}

void TerminalWidget::onProcessFinished(int exitCode)
{
    running_ = false;
    screen_->feed(QStringLiteral("\r\n\x1b[2m[Processus terminé, code %1]\x1b[0m\r\n").arg(exitCode).toUtf8());
    emit finished(exitCode);
}

void TerminalWidget::onScreenChanged()
{
    updateScrollBar();
    viewport()->update();
}

void TerminalWidget::updateGridSize()
{
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
    QScrollBar *bar = verticalScrollBar();
    const QSignalBlocker blocker(bar);
    bar->setRange(0, screen_->scrollbackSize());
    bar->setPageStep(screen_->rows());
    bar->setValue(screen_->scrollbackSize() - scrollOffset_);
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
    if (event.key() >= Qt::Key_1 && event.key() <= Qt::Key_3)
        return true;
    // Touches 1, 2 et 3 de la rangée du haut, quelle que soit la disposition.
#if defined(Q_OS_MACOS)
    const quint32 code = event.nativeVirtualKey(); // kVK_ANSI_1..3
    return code >= 0x12 && code <= 0x14;
#elif defined(Q_OS_WIN)
    const quint32 code = event.nativeScanCode();
    return code >= 0x02 && code <= 0x04;
#else
    const quint32 code = event.nativeScanCode(); // code de touche XKB (evdev + 8)
    return code >= 10 && code <= 12;
#endif
}

bool TerminalWidget::event(QEvent *event)
{
    // Le terminal veut toutes les touches : sans cela, les raccourcis de
    // l'application (Ctrl+W, Ctrl+S...) ne parviendraient pas au programme.
    // Exceptions : F11 et Ctrl+1/2/3, qui changent de mode (Terminal, Edit, Diff).
    if (event->type() == QEvent::ShortcutOverride) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_F11 || isModeShortcut(*key))
            return QAbstractScrollArea::event(event);
        event->accept();
        return true;
    }
    return QAbstractScrollArea::event(event);
}

bool TerminalWidget::focusNextPrevChild(bool)
{
    return false; // Tab et Maj+Tab vont au programme (complétion, modes de Claude)
}

void TerminalWidget::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateGridSize();
}

void TerminalWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), background_);

    for (int row = 0; row < screen_->rows(); ++row) {
        const int contentRow = row - scrollOffset_;
        for (int column = 0; column < screen_->columns(); ++column) {
            const TerminalCell cell = screen_->cell(contentRow, column);
            if (cell.isContinuation)
                continue;
            paintCell(painter, cell, column * cellSize_.width(), row * cellSize_.height(),
                      isSelected(contentRow, column));
        }
    }
    if (scrollOffset_ == 0)
        paintCursor(painter);
}

void TerminalWidget::paintCell(QPainter &painter, const TerminalCell &cell, int x, int y, bool selected) const
{
    QColor foreground = cell.foreground;
    QColor background = cell.background;
    if (selected)
        std::swap(foreground, background);

    const QRect area(x, y, cellSize_.width() * cell.width, cellSize_.height());
    if (background != background_)
        painter.fillRect(area, background);
    if (cell.text.isEmpty() || cell.text == QLatin1String(" "))
        return;

    QFont font = cell.bold ? boldFont_ : cell.italic ? italicFont_ : font_;
    font.setUnderline(cell.underline);
    font.setStrikeOut(cell.strike);
    painter.setFont(font);
    painter.setPen(foreground);
    painter.drawText(x, y + ascent_, cell.text);
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
    paintCell(painter, cell, area.x(), area.y(), false);
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

// Caractères composés par une méthode de saisie (accents morts, IME).
void TerminalWidget::inputMethodEvent(QInputMethodEvent *event)
{
    if (!event->commitString().isEmpty())
        screen_->sendKey(0, Qt::NoModifier, event->commitString());
    event->accept();
}

void TerminalWidget::wheelEvent(QWheelEvent *event)
{
    const int lines = event->angleDelta().y() / 40;
    scrollOffset_ = std::clamp(scrollOffset_ + lines, 0, screen_->scrollbackSize());
    updateScrollBar();
    viewport()->update();
}

QPoint TerminalWidget::cellAt(const QPoint &widgetPosition) const
{
    const int column = std::clamp(widgetPosition.x() / cellSize_.width(), 0, screen_->columns() - 1);
    const int row = std::clamp(widgetPosition.y() / cellSize_.height(), 0, screen_->rows() - 1);
    return QPoint(column, row - scrollOffset_);
}

void TerminalWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    if (event->button() == Qt::MiddleButton) {
        // Collage de la sélection primaire, comme dans les terminaux X11.
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
        selectionAnchor_ = QPoint(-1, 0); // simple clic : pas de sélection
        selectionEnd_ = selectionAnchor_;
    } else if (QApplication::clipboard()->supportsSelection()) {
        copySelection(QClipboard::Selection);
    }
    viewport()->update();
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
    const QString text = QApplication::clipboard()->text();
    if (text.isEmpty())
        return;
    screen_->paste(text);
    scrollToBottom();
}
