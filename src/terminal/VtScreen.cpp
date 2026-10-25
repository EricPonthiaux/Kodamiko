// SPDX-License-Identifier: GPL-3.0-only

#include "VtScreen.h"

#include <algorithm>
#include <utility>

namespace {

constexpr size_t maxScrollbackLines = 10000;

// VTermModifier is a set of bit flags: combinations such as Shift+Ctrl are
// valid values even though the C enum does not name them.
VTermModifier modifierFromBits(int bits)
{
    return static_cast<VTermModifier>(bits); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange): bit flags
}

VTermModifier toVTermModifier(Qt::KeyboardModifiers modifiers)
{
    int result = VTERM_MOD_NONE;
    if (modifiers & Qt::ShiftModifier)
        result |= VTERM_MOD_SHIFT;
    if (modifiers & Qt::AltModifier)
        result |= VTERM_MOD_ALT;
    // On macOS, Qt calls the Cmd key "Control" and the real Ctrl key "Meta":
    // the latter is the one that produces the terminal's Ctrl+C.
#ifdef Q_OS_MACOS
    if (modifiers & Qt::MetaModifier)
        result |= VTERM_MOD_CTRL;
#else
    if (modifiers & Qt::ControlModifier)
        result |= VTERM_MOD_CTRL;
#endif
    return modifierFromBits(result);
}

VTermKey toVTermKey(int key)
{
    switch (key) {
    case Qt::Key_Return:
        return VTERM_KEY_ENTER;
    case Qt::Key_Enter:
        return VTERM_KEY_KP_ENTER;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        return VTERM_KEY_TAB;
    case Qt::Key_Backspace:
        return VTERM_KEY_BACKSPACE;
    case Qt::Key_Escape:
        return VTERM_KEY_ESCAPE;
    case Qt::Key_Up:
        return VTERM_KEY_UP;
    case Qt::Key_Down:
        return VTERM_KEY_DOWN;
    case Qt::Key_Left:
        return VTERM_KEY_LEFT;
    case Qt::Key_Right:
        return VTERM_KEY_RIGHT;
    case Qt::Key_Insert:
        return VTERM_KEY_INS;
    case Qt::Key_Delete:
        return VTERM_KEY_DEL;
    case Qt::Key_Home:
        return VTERM_KEY_HOME;
    case Qt::Key_End:
        return VTERM_KEY_END;
    case Qt::Key_PageUp:
        return VTERM_KEY_PAGEUP;
    case Qt::Key_PageDown:
        return VTERM_KEY_PAGEDOWN;
    default:
        break;
    }
    // libvterm numbers F1…F255 from VTERM_KEY_FUNCTION_0 without naming them.
    if (key < Qt::Key_F1 || key > Qt::Key_F35)
        return VTERM_KEY_NONE;
    const int functionKey = VTERM_KEY_FUNCTION(key - Qt::Key_F1 + 1);
    return static_cast<VTermKey>(functionKey); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange): unnamed keys
}

VTermColor rgbColor(const QColor &color)
{
    VTermColor result;
    vterm_color_rgb(&result, static_cast<uint8_t>(color.red()), static_cast<uint8_t>(color.green()),
                    static_cast<uint8_t>(color.blue()));
    return result;
}

// Blank cell in the default colors.
VTermScreenCell blankCell()
{
    VTermScreenCell blank{};
    blank.width = 1;
    vterm_color_rgb(&blank.fg, 0, 0, 0);
    blank.fg.type = static_cast<uint8_t>(blank.fg.type | VTERM_COLOR_DEFAULT_FG);
    vterm_color_rgb(&blank.bg, 0, 0, 0);
    blank.bg.type = static_cast<uint8_t>(blank.bg.type | VTERM_COLOR_DEFAULT_BG);
    return blank;
}

bool isBlank(const VTermScreenCell &cell)
{
    return cell.chars[0] == 0 && VTERM_COLOR_IS_DEFAULT_BG(&cell.bg) && !cell.attrs.reverse &&
           cell.attrs.underline == VTERM_UNDERLINE_OFF && !cell.attrs.strike;
}

} // namespace

VtScreen::VtScreen(int rows, int columns, QObject *parent)
    : QObject(parent)
    , terminal_(vterm_new(rows, columns))
    , rows_(rows)
    , columns_(columns)
{
    vterm_set_utf8(terminal_, 1);
    vterm_output_set_callback(terminal_, &VtScreen::onOutput, this);

    screen_ = vterm_obtain_screen(terminal_);
    callbacks_.damage = &VtScreen::onDamage;
    callbacks_.movecursor = &VtScreen::onMoveCursor;
    callbacks_.settermprop = &VtScreen::onSetTermProp;
    callbacks_.bell = &VtScreen::onBell;
    callbacks_.sb_pushline = &VtScreen::onPushLine;
    callbacks_.sb_popline = &VtScreen::onPopLine;
    vterm_screen_set_callbacks(screen_, &callbacks_, this);
    // Damage is merged until the end of each feed(): one notification per
    // batch of output instead of one per cell.
    vterm_screen_set_damage_merge(screen_, VTERM_DAMAGE_SCREEN);
    vterm_screen_enable_altscreen(screen_, 1);
    vterm_screen_enable_reflow(screen_, true);
    vterm_screen_reset(screen_, 1);
}

VtScreen::~VtScreen()
{
    vterm_free(terminal_);
}

void VtScreen::feed(const QByteArray &data)
{
    vterm_input_write(terminal_, data.constData(), static_cast<size_t>(data.size()));
    vterm_screen_flush_damage(screen_);
    if (changed_) {
        changed_ = false;
        emit screenChanged();
    }
}

void VtScreen::resize(int rows, int columns)
{
    rows = std::max(rows, 1);
    columns = std::max(columns, 1);
    if (rows == rows_ && columns == columns_)
        return;
    rows_ = rows;
    columns_ = columns;
    vterm_set_size(terminal_, rows, columns);
    vterm_screen_flush_damage(screen_);
    addDamage(QRect(0, 0, columns_, rows_));
    changed_ = false;
    emit screenChanged();
}

void VtScreen::setDefaultColors(const QColor &foreground, const QColor &background)
{
    defaultForeground_ = foreground;
    defaultBackground_ = background;
    const VTermColor fg = rgbColor(foreground);
    const VTermColor bg = rgbColor(background);
    vterm_screen_set_default_colors(screen_, &fg, &bg);
    addDamage(QRect(0, 0, columns_, rows_));
    emit screenChanged();
}

TerminalCell VtScreen::cell(int row, int column) const
{
    if (row >= 0) {
        VTermScreenCell cell;
        if (!vterm_screen_get_cell(screen_, VTermPos{row, column}, &cell))
            return {};
        return toTerminalCell(cell);
    }

    // History: row -1 is the most recent.
    const int index = static_cast<int>(scrollback_.size()) + row;
    if (index < 0)
        return {};
    const std::vector<VTermScreenCell> &line = scrollback_[static_cast<size_t>(index)];
    if (column >= static_cast<int>(line.size())) {
        TerminalCell blank;
        blank.foreground = defaultForeground_;
        blank.background = defaultBackground_;
        return blank;
    }
    TerminalCell result = toTerminalCell(line[static_cast<size_t>(column)]);
    // Widths are not stored in the history: we infer them.
    if (column + 1 < static_cast<int>(line.size()) && line[static_cast<size_t>(column) + 1].chars[0] == uint32_t(-1))
        result.width = 2;
    return result;
}

void VtScreen::readRow(int row, std::vector<VTermScreenCell> &cells) const
{
    cells.resize(static_cast<size_t>(columns_));
    if (row >= 0) {
        for (int column = 0; column < columns_; ++column) {
            if (!vterm_screen_get_cell(screen_, VTermPos{row, column}, &cells[static_cast<size_t>(column)]))
                cells[static_cast<size_t>(column)] = blankCell();
        }
        return;
    }

    const int index = static_cast<int>(scrollback_.size()) + row;
    if (index < 0) {
        std::fill(cells.begin(), cells.end(), blankCell());
        return;
    }
    const std::vector<VTermScreenCell> &line = scrollback_[static_cast<size_t>(index)];
    const size_t stored = std::min(line.size(), cells.size());
    std::copy_n(line.begin(), stored, cells.begin());
    std::fill(cells.begin() + static_cast<std::ptrdiff_t>(stored), cells.end(), blankCell());
    // Widths are not stored in the history: we infer them.
    for (size_t column = 0; column + 1 < stored; ++column) {
        if (cells[column + 1].chars[0] == uint32_t(-1))
            cells[column].width = 2;
    }
}

void VtScreen::colorsOf(const VTermScreenCell &cell, QRgb &foreground, QRgb &background) const
{
    foreground = toRgb(cell.fg, true);
    background = toRgb(cell.bg, false);
    if (cell.attrs.reverse)
        std::swap(foreground, background);
}

QRect VtScreen::takeDamage()
{
    return std::exchange(damage_, QRect());
}

void VtScreen::addDamage(const QRect &cells)
{
    damage_ |= cells;
    changed_ = true;
}

QString VtScreen::lineText(int row) const
{
    QString text;
    for (int column = 0; column < columns_; ++column) {
        const TerminalCell c = cell(row, column);
        if (c.isContinuation)
            continue;
        text += c.text.isEmpty() ? QStringLiteral(" ") : c.text;
    }
    while (text.endsWith(QLatin1Char(' ')))
        text.chop(1);
    return text;
}

bool VtScreen::sendKey(int key, Qt::KeyboardModifiers modifiers, const QString &text)
{
    const VTermModifier modifier = toVTermModifier(modifiers);

    if (key == Qt::Key_Backtab) {
        vterm_keyboard_key(terminal_, VTERM_KEY_TAB, modifierFromBits(modifier | VTERM_MOD_SHIFT));
        return true;
    }
    if (const VTermKey special = toVTermKey(key); special != VTERM_KEY_NONE) {
        vterm_keyboard_key(terminal_, special, modifier);
        return true;
    }

    // Ctrl+letter: libvterm computes the control code itself (Ctrl+C → 0x03).
    if ((modifier & VTERM_MOD_CTRL) && key >= Qt::Key_A && key <= Qt::Key_Z) {
        const auto letter = static_cast<uint32_t>('a' + (key - Qt::Key_A));
        vterm_keyboard_unichar(terminal_, letter, modifierFromBits(modifier & ~VTERM_MOD_SHIFT));
        return true;
    }

    if (text.isEmpty())
        return false;
    // Uppercase letters and symbols already carry the effect of Shift: don't send it again.
    const VTermModifier withoutShift = modifierFromBits(modifier & ~VTERM_MOD_SHIFT);
    for (const char32_t codePoint : text.toUcs4()) {
        if (codePoint < 0x20 && !(modifier & VTERM_MOD_CTRL))
            continue; // control character produced by the platform, already handled
        vterm_keyboard_unichar(terminal_, codePoint, withoutShift);
    }
    return true;
}

// Bracketed paste (if the program requested it) lets it tell a paste apart
// from typing: Claude Code does not submit a pasted prompt that contains
// line breaks.
void VtScreen::sendWheel(int row, int column, bool up, Qt::KeyboardModifiers modifiers)
{
    const VTermModifier modifier = toVTermModifier(modifiers);
    vterm_mouse_move(terminal_, row, column, modifier);
    // Buttons 4 and 5: the wheel, a press without release.
    vterm_mouse_button(terminal_, up ? 4 : 5, true, modifier);
}

void VtScreen::sendLeftButtonPress(QPoint cell, Qt::KeyboardModifiers modifiers)
{
    const VTermModifier modifier = toVTermModifier(modifiers);
    vterm_mouse_move(terminal_, cell.y(), cell.x(), modifier);
    vterm_mouse_button(terminal_, 1, true, modifier);
}

void VtScreen::sendMouseMove(QPoint cell, Qt::KeyboardModifiers modifiers)
{
    vterm_mouse_move(terminal_, cell.y(), cell.x(), toVTermModifier(modifiers));
}

void VtScreen::sendLeftButtonRelease(QPoint cell, Qt::KeyboardModifiers modifiers)
{
    const VTermModifier modifier = toVTermModifier(modifiers);
    vterm_mouse_move(terminal_, cell.y(), cell.x(), modifier);
    vterm_mouse_button(terminal_, 1, false, modifier);
}

void VtScreen::paste(const QString &text)
{
    vterm_keyboard_start_paste(terminal_);
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\r")).replace(QLatin1Char('\n'), QLatin1Char('\r'));
    for (const char32_t codePoint : normalized.toUcs4())
        vterm_keyboard_unichar(terminal_, codePoint, VTERM_MOD_NONE);
    vterm_keyboard_end_paste(terminal_);
}

TerminalCell VtScreen::toTerminalCell(const VTermScreenCell &cell) const
{
    TerminalCell result;
    if (cell.chars[0] == uint32_t(-1)) {
        result.isContinuation = true;
    } else {
        for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0; ++i)
            result.text += QString::fromUcs4(reinterpret_cast<const char32_t *>(&cell.chars[i]), 1);
    }
    result.foreground = toQColor(cell.fg, true);
    result.background = toQColor(cell.bg, false);
    if (cell.attrs.reverse)
        std::swap(result.foreground, result.background);
    result.bold = cell.attrs.bold;
    result.italic = cell.attrs.italic;
    result.underline = cell.attrs.underline != VTERM_UNDERLINE_OFF;
    result.strike = cell.attrs.strike;
    result.width = static_cast<unsigned char>(cell.width);
    return result;
}

QColor VtScreen::toQColor(VTermColor color, bool isForeground) const
{
    if (isForeground && VTERM_COLOR_IS_DEFAULT_FG(&color))
        return defaultForeground_;
    if (!isForeground && VTERM_COLOR_IS_DEFAULT_BG(&color))
        return defaultBackground_;
    vterm_screen_convert_color_to_rgb(screen_, &color);
    return {color.rgb.red, color.rgb.green, color.rgb.blue};
}

QRgb VtScreen::toRgb(VTermColor color, bool isForeground) const
{
    if (isForeground && VTERM_COLOR_IS_DEFAULT_FG(&color))
        return defaultForeground_.rgb();
    if (!isForeground && VTERM_COLOR_IS_DEFAULT_BG(&color))
        return defaultBackground_.rgb();
    vterm_screen_convert_color_to_rgb(screen_, &color);
    return qRgb(color.rgb.red, color.rgb.green, color.rgb.blue);
}

int VtScreen::onDamage(VTermRect rect, void *user)
{
    static_cast<VtScreen *>(user)->addDamage(
        QRect(rect.start_col, rect.start_row, rect.end_col - rect.start_col, rect.end_row - rect.start_row));
    return 1;
}

int VtScreen::onMoveCursor(VTermPos position, VTermPos oldPosition, int visible, void *user)
{
    auto *self = static_cast<VtScreen *>(user);
    self->cursor_ = QPoint(position.col, position.row);
    self->cursorVisible_ = visible;
    self->addDamage(QRect(oldPosition.col, oldPosition.row, 1, 1));
    self->addDamage(QRect(position.col, position.row, 1, 1));
    return 1;
}

int VtScreen::onSetTermProp(VTermProp property, VTermValue *value, void *user)
{
    auto *self = static_cast<VtScreen *>(user);
    switch (property) {
    case VTERM_PROP_CURSORVISIBLE:
        self->cursorVisible_ = value->boolean;
        self->addDamage(QRect(self->cursor_, QSize(1, 1)));
        break;
    case VTERM_PROP_ALTSCREEN:
        self->alternateScreen_ = value->boolean;
        break;
    case VTERM_PROP_MOUSE:
        self->mouseReporting_ = value->number != VTERM_PROP_MOUSE_NONE;
        break;
    case VTERM_PROP_TITLE: {
        // The title may arrive in several fragments.
        const VTermStringFragment &fragment = value->string;
        if (fragment.initial)
            self->titleBuffer_.clear();
        self->titleBuffer_ += QString::fromUtf8(fragment.str, static_cast<qsizetype>(fragment.len));
        if (fragment.final)
            emit self->titleChanged(self->titleBuffer_);
        break;
    }
    default:
        break;
    }
    return 1;
}

int VtScreen::onBell(void *user)
{
    emit static_cast<VtScreen *>(user)->bell();
    return 1;
}

int VtScreen::onPushLine(int columns, const VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<VtScreen *>(user);
    int stored = columns;
    while (stored > 0 && isBlank(cells[stored - 1]))
        --stored;
    self->scrollback_.emplace_back(cells, cells + stored);
    if (self->scrollback_.size() > maxScrollbackLines)
        self->scrollback_.pop_front();
    return 1;
}

// When the screen grows, libvterm asks for the most recently scrolled-out lines back.
int VtScreen::onPopLine(int columns, VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<VtScreen *>(user);
    if (self->scrollback_.empty())
        return 0;

    const std::vector<VTermScreenCell> &line = self->scrollback_.back();
    const int copied = std::min(columns, static_cast<int>(line.size()));
    std::copy_n(line.begin(), copied, cells);
    if (copied < columns)
        std::fill(cells + copied, cells + columns, blankCell());
    self->scrollback_.pop_back();
    return 1;
}

void VtScreen::onOutput(const char *bytes, size_t length, void *user)
{
    emit static_cast<VtScreen *>(user)->outputReady(QByteArray(bytes, static_cast<qsizetype>(length)));
}
