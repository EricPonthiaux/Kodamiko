// SPDX-License-Identifier: GPL-3.0-only

#include "ScintillaView.h"

#include "system/Host.h"

#include <QAction>
#include <QImage>
#include <QMenu>
#include <QPoint>

#include <array>

namespace {

using Scintilla::FlagSet;

constexpr std::string_view braces = "()[]{}";
constexpr int foldMarginWidthPx = 16;

Qt::KeyboardModifiers qtModifiers(Scintilla::KeyMod modifiers)
{
    Qt::KeyboardModifiers result;
    if (FlagSet(modifiers, Scintilla::KeyMod::Shift))
        result |= Qt::ShiftModifier;
    if (FlagSet(modifiers, Scintilla::KeyMod::Ctrl))
        result |= Qt::ControlModifier;
    if (FlagSet(modifiers, Scintilla::KeyMod::Alt))
        result |= Qt::AltModifier;
    if (FlagSet(modifiers, Scintilla::KeyMod::Meta))
        result |= Qt::MetaModifier;
    return result;
}

struct KeyBinding {
    int key;
    int modifiers;
    int command;
};

// macOS's text-editing keys, where Scintilla's Qt widget keeps Windows's
// (Qt gives Cmd as Ctrl): Cmd moves to the line's or the document's ends,
// Option by words.
constexpr std::array macOSKeys{
    KeyBinding{SCK_LEFT, SCMOD_CTRL, SCI_VCHOME},
    KeyBinding{SCK_LEFT, SCMOD_CTRL | SCMOD_SHIFT, SCI_VCHOMEEXTEND},
    KeyBinding{SCK_RIGHT, SCMOD_CTRL, SCI_LINEEND},
    KeyBinding{SCK_RIGHT, SCMOD_CTRL | SCMOD_SHIFT, SCI_LINEENDEXTEND},
    KeyBinding{SCK_UP, SCMOD_CTRL, SCI_DOCUMENTSTART},
    KeyBinding{SCK_UP, SCMOD_CTRL | SCMOD_SHIFT, SCI_DOCUMENTSTARTEXTEND},
    KeyBinding{SCK_DOWN, SCMOD_CTRL, SCI_DOCUMENTEND},
    KeyBinding{SCK_DOWN, SCMOD_CTRL | SCMOD_SHIFT, SCI_DOCUMENTENDEXTEND},
    KeyBinding{SCK_LEFT, SCMOD_ALT, SCI_WORDLEFT},
    KeyBinding{SCK_LEFT, SCMOD_ALT | SCMOD_SHIFT, SCI_WORDLEFTEXTEND},
    KeyBinding{SCK_RIGHT, SCMOD_ALT, SCI_WORDRIGHTEND},
    KeyBinding{SCK_RIGHT, SCMOD_ALT | SCMOD_SHIFT, SCI_WORDRIGHTENDEXTEND},
    KeyBinding{SCK_BACK, SCMOD_ALT, SCI_DELWORDLEFT},
    KeyBinding{SCK_BACK, SCMOD_CTRL, SCI_DELLINELEFT},
    KeyBinding{SCK_DELETE, SCMOD_ALT, SCI_DELWORDRIGHT},
    KeyBinding{SCK_DELETE, SCMOD_CTRL, SCI_DELLINERIGHT},
    KeyBinding{'Z', SCMOD_CTRL | SCMOD_SHIFT, SCI_REDO},
};

// Lifts the read-only flag while the program writes the text: it only
// stops the user (Scintilla refuses every change otherwise).
class ProgrammaticEdit {
public:
    explicit ProgrammaticEdit(const ScintillaEditBase &view)
        : view_(view)
        , wasReadOnly_(view.send(SCI_GETREADONLY) != 0)
    {
        view_.send(SCI_SETREADONLY, false);
    }

    ~ProgrammaticEdit() { view_.send(SCI_SETREADONLY, wasReadOnly_); }

    ProgrammaticEdit(const ProgrammaticEdit &) = delete;
    ProgrammaticEdit &operator=(const ProgrammaticEdit &) = delete;

private:
    const ScintillaEditBase &view_;
    bool wasReadOnly_;
};

} // namespace

sptr_t scintillaColor(const QColor &color)
{
    return sptr_t(color.red()) | sptr_t(color.green()) << 8 | sptr_t(color.blue()) << 16;
}

sptr_t scintillaColorWithAlpha(const QColor &color)
{
    return scintillaColor(color) | sptr_t(color.alpha()) << 24;
}

ScintillaView::ScintillaView(QWidget *parent)
    : ScintillaEditBase(parent)
{
    send(SCI_SETCODEPAGE, SC_CP_UTF8);
    // Hover effects (dwell, hotspots, the margins' marks) need the moves
    // without a button pressed, which reach the viewport.
    viewport()->setMouseTracking(true);
    // A line made visible lands a few lines away from the edge, as in SciTE.
    send(SCI_SETVISIBLEPOLICY, VISIBLE_STRICT | VISIBLE_SLOP, 4);
    send(SCI_AUTOCSETCASEINSENSITIVEBEHAVIOUR, SC_CASEINSENSITIVEBEHAVIOUR_IGNORECASE);
    bindSystemKeys();

    connect(this, &ScintillaEditBase::modified, this,
            [this](Scintilla::ModificationFlags type, Scintilla::Position, Scintilla::Position,
                   Scintilla::Position linesAdded) { onModified(type, linesAdded); });
    connect(this, &ScintillaEditBase::savePointChanged, this, &ScintillaView::modificationChanged);
    connect(this, &ScintillaEditBase::updateUi, this, &ScintillaView::onUpdateUi);
    connect(this, &ScintillaEditBase::marginClicked, this, &ScintillaView::onMarginClicked);
    connect(this, &ScintillaEditBase::charAdded, this, &ScintillaView::onCharAdded);
    connect(this, &ScintillaEditBase::notify, this, &ScintillaView::onNotification);
}

const char *ScintillaView::characterPointer() const
{
    // NOLINTNEXTLINE(performance-no-int-to-ptr): Scintilla returns pointers as integers.
    return reinterpret_cast<const char *>(send(SCI_GETCHARACTERPOINTER));
}

QString ScintillaView::text() const
{
    const sptr_t length = send(SCI_GETTEXTLENGTH);
    QByteArray bytes(length + 1, '\0');
    send(SCI_GETTEXT, uptr_t(length + 1), reinterpret_cast<sptr_t>(bytes.data()));
    bytes.chop(1);
    return QString::fromUtf8(bytes);
}

QString ScintillaView::text(int line) const
{
    const sptr_t length = send(SCI_LINELENGTH, uptr_t(line));
    QByteArray bytes(length, '\0');
    send(SCI_GETLINE, uptr_t(line), reinterpret_cast<sptr_t>(bytes.data()));
    return QString::fromUtf8(bytes);
}

QString ScintillaView::text(int start, int end) const
{
    return QString::fromUtf8(utf8Range(start, end));
}

void ScintillaView::setText(const QString &text)
{
    const ProgrammaticEdit edit(*this);
    sends(SCI_SETTEXT, 0, text.toUtf8().constData());
}

int ScintillaView::length() const
{
    return int(send(SCI_GETLENGTH));
}

int ScintillaView::lines() const
{
    return int(send(SCI_GETLINECOUNT));
}

void ScintillaView::insert(const QString &text)
{
    const ProgrammaticEdit edit(*this);
    sends(SCI_INSERTTEXT, uptr_t(-1), text.toUtf8().constData());
}

void ScintillaView::insertAt(const QString &text, int line, int index)
{
    const ProgrammaticEdit edit(*this);
    sends(SCI_INSERTTEXT, uptr_t(positionFromLineIndex(line, index)), text.toUtf8().constData());
}

void ScintillaView::append(const QString &text)
{
    const ProgrammaticEdit edit(*this);
    const QByteArray bytes = text.toUtf8();
    sends(SCI_APPENDTEXT, uptr_t(bytes.size()), bytes.constData());
}

QString ScintillaView::selectedText() const
{
    return QString::fromUtf8(utf8Range(int(send(SCI_GETSELECTIONSTART)), int(send(SCI_GETSELECTIONEND))));
}

bool ScintillaView::hasSelectedText() const
{
    return !send(SCI_GETSELECTIONEMPTY);
}

void ScintillaView::removeSelectedText()
{
    sends(SCI_REPLACESEL, 0, "");
}

void ScintillaView::replaceSelectedText(const QString &text)
{
    sends(SCI_REPLACESEL, 0, text.toUtf8().constData());
}

void ScintillaView::selectAll(bool select)
{
    if (select)
        send(SCI_SELECTALL);
    else
        send(SCI_SETEMPTYSELECTION, uptr_t(send(SCI_GETCURRENTPOS)));
}

void ScintillaView::getCursorPosition(int *line, int *index) const
{
    lineIndexFromPosition(int(send(SCI_GETCURRENTPOS)), line, index);
}

void ScintillaView::setCursorPosition(int line, int index)
{
    send(SCI_GOTOPOS, uptr_t(positionFromLineIndex(line, index)));
}

void ScintillaView::getSelection(int *lineFrom, int *indexFrom, int *lineTo, int *indexTo) const
{
    if (!hasSelectedText()) {
        *lineFrom = *indexFrom = *lineTo = *indexTo = -1;
        return;
    }
    lineIndexFromPosition(int(send(SCI_GETSELECTIONSTART)), lineFrom, indexFrom);
    lineIndexFromPosition(int(send(SCI_GETSELECTIONEND)), lineTo, indexTo);
}

void ScintillaView::setSelection(int lineFrom, int indexFrom, int lineTo, int indexTo)
{
    send(SCI_SETSEL, uptr_t(positionFromLineIndex(lineFrom, indexFrom)), positionFromLineIndex(lineTo, indexTo));
}

int ScintillaView::positionFromLineIndex(int line, int index) const
{
    const sptr_t lineStart = send(SCI_POSITIONFROMLINE, uptr_t(line));
    if (index <= 0)
        return int(lineStart);
    const sptr_t position = send(SCI_POSITIONRELATIVE, uptr_t(lineStart), index);
    // Past the end, Scintilla answers 0.
    return int(position > 0 ? position : send(SCI_GETLENGTH));
}

void ScintillaView::lineIndexFromPosition(int position, int *line, int *index) const
{
    *line = int(send(SCI_LINEFROMPOSITION, uptr_t(position)));
    *index = int(send(SCI_COUNTCHARACTERS, uptr_t(send(SCI_POSITIONFROMLINE, uptr_t(*line))), position));
}

int ScintillaView::lineAt(const QPoint &point) const
{
    const sptr_t position = send(SCI_POSITIONFROMPOINTCLOSE, uptr_t(point.x()), point.y());
    return position < 0 ? -1 : int(send(SCI_LINEFROMPOSITION, uptr_t(position)));
}

int ScintillaView::firstVisibleLine() const
{
    return int(send(SCI_GETFIRSTVISIBLELINE));
}

void ScintillaView::setFirstVisibleLine(int line)
{
    send(SCI_SETFIRSTVISIBLELINE, uptr_t(line));
}

void ScintillaView::ensureLineVisible(int line)
{
    send(SCI_ENSUREVISIBLEENFORCEPOLICY, uptr_t(line));
}

QString ScintillaView::wordAtLineIndex(int line, int index) const
{
    const auto position = uptr_t(positionFromLineIndex(line, index));
    return QString::fromUtf8(
        utf8Range(int(send(SCI_WORDSTARTPOSITION, position, true)), int(send(SCI_WORDENDPOSITION, position, true))));
}

int ScintillaView::indentation(int line) const
{
    return int(send(SCI_GETLINEINDENTATION, uptr_t(line)));
}

void ScintillaView::setIndentation(int line, int indentation)
{
    send(SCI_SETLINEINDENTATION, uptr_t(line), indentation);
}

bool ScintillaView::isModified() const
{
    return send(SCI_GETMODIFY) != 0;
}

void ScintillaView::setModified(bool modified)
{
    // Scintilla can only come back to a save point, not leave it.
    if (!modified)
        send(SCI_SETSAVEPOINT);
}

bool ScintillaView::isReadOnly() const
{
    return send(SCI_GETREADONLY) != 0;
}

void ScintillaView::setReadOnly(bool readOnly)
{
    send(SCI_SETREADONLY, readOnly);
}

void ScintillaView::undo()
{
    send(SCI_UNDO);
}

void ScintillaView::redo()
{
    send(SCI_REDO);
}

void ScintillaView::beginUndoAction()
{
    send(SCI_BEGINUNDOACTION);
}

void ScintillaView::endUndoAction()
{
    send(SCI_ENDUNDOACTION);
}

void ScintillaView::cut()
{
    send(SCI_CUT);
}

void ScintillaView::copy()
{
    send(SCI_COPY);
}

void ScintillaView::paste()
{
    send(SCI_PASTE);
}

void ScintillaView::zoomIn()
{
    send(SCI_ZOOMIN);
}

void ScintillaView::zoomOut()
{
    send(SCI_ZOOMOUT);
}

void ScintillaView::zoomTo(int size)
{
    send(SCI_SETZOOM, uptr_t(size));
}

bool ScintillaView::isUndoAvailable() const
{
    return send(SCI_CANUNDO) != 0;
}

bool ScintillaView::isRedoAvailable() const
{
    return send(SCI_CANREDO) != 0;
}

int ScintillaView::tabWidth() const
{
    return int(send(SCI_GETTABWIDTH));
}

bool ScintillaView::indentationsUseTabs() const
{
    return send(SCI_GETUSETABS) != 0;
}

void ScintillaView::setCaretLineVisible(bool isVisible)
{
    send(SCI_SETCARETLINEVISIBLEALWAYS, isVisible);
    send(SCI_SETCARETLINEVISIBLE, isVisible);
}

void ScintillaView::recolor()
{
    send(SCI_COLOURISE, 0, -1);
}

std::unique_ptr<QMenu> ScintillaView::createStandardContextMenu()
{
    auto menu = std::make_unique<QMenu>(this);
    const bool isEditable = !isReadOnly();
    const auto add = [this, &menu](const QString &text, int message, bool isEnabled) {
        QAction *action = menu->addAction(text, this, [this, message] { send(unsigned(message)); });
        action->setEnabled(isEnabled);
    };
    if (isEditable) {
        add(tr("&Undo"), SCI_UNDO, isUndoAvailable());
        add(tr("&Redo"), SCI_REDO, isRedoAvailable());
        menu->addSeparator();
    }
    const bool hasSelection = hasSelectedText();
    if (isEditable)
        add(tr("Cu&t"), SCI_CUT, hasSelection);
    add(tr("&Copy"), SCI_COPY, hasSelection);
    if (isEditable) {
        add(tr("&Paste"), SCI_PASTE, send(SCI_CANPASTE) != 0);
        add(tr("Delete"), SCI_CLEAR, hasSelection);
    }
    menu->addSeparator();
    add(tr("Select All"), SCI_SELECTALL, length() > 0);
    return menu;
}

void ScintillaView::markerDefine(int symbol, int marker)
{
    send(SCI_MARKERDEFINE, uptr_t(marker), symbol);
}

void ScintillaView::markerDefine(const QImage &image, int marker)
{
    // Scintilla wants unpremultiplied RGBA bytes.
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    send(SCI_RGBAIMAGESETWIDTH, uptr_t(rgba.width()));
    send(SCI_RGBAIMAGESETHEIGHT, uptr_t(rgba.height()));
    send(SCI_MARKERDEFINERGBAIMAGE, uptr_t(marker), reinterpret_cast<sptr_t>(rgba.constBits()));
}

void ScintillaView::setMarkerBackgroundColor(const QColor &color, int marker)
{
    send(SCI_MARKERSETBACK, uptr_t(marker), scintillaColor(color));
    send(SCI_MARKERSETALPHA, uptr_t(marker), color.alpha());
}

void ScintillaView::setMarkerForegroundColor(const QColor &color, int marker)
{
    send(SCI_MARKERSETFORE, uptr_t(marker), scintillaColor(color));
}

int ScintillaView::markerAdd(int line, int marker)
{
    return int(send(SCI_MARKERADD, uptr_t(line), marker));
}

void ScintillaView::markerDelete(int line, int marker)
{
    send(SCI_MARKERDELETE, uptr_t(line), marker);
}

void ScintillaView::markerDeleteAll(int marker)
{
    send(SCI_MARKERDELETEALL, uptr_t(marker));
}

void ScintillaView::setMarginType(int margin, int type)
{
    send(SCI_SETMARGINTYPEN, uptr_t(margin), type);
}

void ScintillaView::setMarginWidth(int margin, int widthPx)
{
    send(SCI_SETMARGINWIDTHN, uptr_t(margin), widthPx);
}

void ScintillaView::setMarginWidth(int margin, const QString &sample)
{
    const sptr_t width = sends(SCI_TEXTWIDTH, STYLE_LINENUMBER, sample.toUtf8().constData());
    // A little room on each side of the text, as in the line numbers.
    constexpr int paddingPx = 4;
    setMarginWidth(margin, int(width > 0 ? width + paddingPx : 0));
}

int ScintillaView::marginWidth(int margin) const
{
    return int(send(SCI_GETMARGINWIDTHN, uptr_t(margin)));
}

void ScintillaView::setMarginMarkerMask(int margin, int mask)
{
    send(SCI_SETMARGINMASKN, uptr_t(margin), mask);
}

void ScintillaView::setMarginSensitivity(int margin, bool isSensitive)
{
    send(SCI_SETMARGINSENSITIVEN, uptr_t(margin), isSensitive);
}

void ScintillaView::setMarginText(int line, const QString &text, int style)
{
    sends(SCI_MARGINSETTEXT, uptr_t(line), text.toUtf8().constData());
    send(SCI_MARGINSETSTYLE, uptr_t(line), style);
}

void ScintillaView::clearMarginText()
{
    send(SCI_MARGINTEXTCLEARALL);
}

void ScintillaView::setAutoIndent(bool isEnabled)
{
    isAutoIndentEnabled_ = isEnabled;
}

void ScintillaView::setBraceMatching(bool isEnabled)
{
    isBraceMatchingEnabled_ = isEnabled;
    if (!isEnabled)
        send(SCI_BRACEHIGHLIGHT, uptr_t(INVALID_POSITION), INVALID_POSITION);
}

void ScintillaView::setFolding(bool isEnabled, int margin)
{
    foldMargin_ = isEnabled ? margin : -1;
    // A plus on a folded block, a minus on an open one, nothing along it.
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDER, SC_MARK_PLUS);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_MINUS);
    for (const int marker : {SC_MARKNUM_FOLDEREND, SC_MARKNUM_FOLDEROPENMID, SC_MARKNUM_FOLDERMIDTAIL,
                             SC_MARKNUM_FOLDERTAIL, SC_MARKNUM_FOLDERSUB})
        send(SCI_MARKERDEFINE, uptr_t(marker), SC_MARK_EMPTY);
    send(SCI_SETMARGINTYPEN, uptr_t(margin), SC_MARGIN_SYMBOL);
    send(SCI_SETMARGINMASKN, uptr_t(margin), isEnabled ? SC_MASK_FOLDERS : 0);
    send(SCI_SETMARGINSENSITIVEN, uptr_t(margin), isEnabled);
    send(SCI_SETMARGINWIDTHN, uptr_t(margin), isEnabled ? foldMarginWidthPx : 0);
    sends(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold"), isEnabled ? "1" : "0");
}

void ScintillaView::onNotification(const Scintilla::NotificationData *notification)
{
    // Scintilla's other notifications have their own signals.
    switch (notification->nmhdr.code) {
    case Scintilla::Notification::DwellStart:
        emit dwellStarted(int(notification->position), QPoint(notification->x, notification->y));
        break;
    case Scintilla::Notification::DwellEnd:
        emit dwellEnded();
        break;
    case Scintilla::Notification::HotSpotReleaseClick:
        emit hotspotReleased(int(notification->position));
        break;
    default:
        break;
    }
}

void ScintillaView::onModified(Scintilla::ModificationFlags type, Scintilla::Position linesAdded)
{
    const bool isTextChange = FlagSet(type, Scintilla::ModificationFlags::InsertText) ||
                              FlagSet(type, Scintilla::ModificationFlags::DeleteText);
    if (!isTextChange)
        return;
    emit textChanged();
    if (linesAdded != 0)
        emit linesChanged();
}

void ScintillaView::onUpdateUi(Scintilla::Update updated)
{
    if (isBraceMatchingEnabled_)
        matchBraces();
    if (!FlagSet(updated, Scintilla::Update::Selection))
        return;
    const sptr_t caret = send(SCI_GETCURRENTPOS);
    const sptr_t anchor = send(SCI_GETANCHOR);
    const bool caretMoved = caret != lastCaret_;
    const bool selectionMoved = caretMoved || anchor != lastAnchor_;
    lastCaret_ = caret;
    lastAnchor_ = anchor;
    if (caretMoved)
        emit cursorPositionChanged(int(send(SCI_LINEFROMPOSITION, uptr_t(caret))),
                                   int(send(SCI_GETCOLUMN, uptr_t(caret))));
    if (selectionMoved)
        emit selectionChanged();
}

void ScintillaView::onMarginClicked(Scintilla::Position position, Scintilla::KeyMod modifiers, int margin)
{
    const int line = int(send(SCI_LINEFROMPOSITION, uptr_t(position)));
    if (margin == foldMargin_) {
        send(SCI_TOGGLEFOLD, uptr_t(line));
        return;
    }
    emit marginLineClicked(margin, line, qtModifiers(modifiers));
}

void ScintillaView::onCharAdded(int character)
{
    // A line end is one or two characters: the last one says the line is new.
    const int lineEndCharacter = send(SCI_GETEOLMODE) == SC_EOL_CR ? int('\r') : int('\n');
    if (isAutoIndentEnabled_ && character == lineEndCharacter)
        indentNewLine();
}

void ScintillaView::indentNewLine()
{
    const auto line = uptr_t(send(SCI_LINEFROMPOSITION, uptr_t(send(SCI_GETCURRENTPOS))));
    if (line == 0)
        return;
    send(SCI_SETLINEINDENTATION, line, send(SCI_GETLINEINDENTATION, line - 1));
    send(SCI_GOTOPOS, uptr_t(send(SCI_GETLINEINDENTPOSITION, line)));
}

void ScintillaView::matchBraces()
{
    const sptr_t caret = send(SCI_GETCURRENTPOS);
    const auto isBraceAt = [this](sptr_t position) {
        return position >= 0 && braces.contains(char(send(SCI_GETCHARAT, uptr_t(position))));
    };
    // The brace before the caret first, as most editors do.
    sptr_t brace = INVALID_POSITION;
    if (isBraceAt(caret - 1))
        brace = caret - 1;
    else if (isBraceAt(caret))
        brace = caret;
    if (brace == INVALID_POSITION) {
        send(SCI_BRACEHIGHLIGHT, uptr_t(INVALID_POSITION), INVALID_POSITION);
        return;
    }
    const sptr_t match = send(SCI_BRACEMATCH, uptr_t(brace), 0);
    if (match == INVALID_POSITION)
        send(SCI_BRACEBADLIGHT, uptr_t(brace));
    else
        send(SCI_BRACEHIGHLIGHT, uptr_t(brace), match);
}

void ScintillaView::bindSystemKeys()
{
    if (Host::operatingSystem() != OperatingSystem::MacOS)
        return;
    for (const KeyBinding &binding : macOSKeys)
        send(SCI_ASSIGNCMDKEY, uptr_t(binding.key) + (uptr_t(binding.modifiers) << 16), binding.command);
}

QByteArray ScintillaView::utf8Range(int start, int end) const
{
    if (end <= start)
        return {};
    QByteArray bytes(end - start + 1, '\0'); // Scintilla adds a NUL
    Sci_TextRangeFull range{{start, end}, bytes.data()};
    send(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<sptr_t>(&range));
    bytes.chop(1);
    return bytes;
}
