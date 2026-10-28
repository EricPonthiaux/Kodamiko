// SPDX-License-Identifier: GPL-3.0-only

#include "EditorWidget.h"

#include "ChangeGutter.h"
#include "CodeFormatter.h"
#include "MergeConflicts.h"
#include "SymbolOccurrences.h"
#include "syntax/HighlighterFactory.h"
#include "syntax/ISyntaxHighlighter.h"
#include "syntax/LanguageRegistry.h"
#include "syntax/ScintillaStyles.h"
#include "ui/ContextMenus.h"

#include <QContextMenuEvent>
#include <QFile>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPointer>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>
#include <Qsci/qscicommandset.h>

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace {

// Margins, from the left: line numbers, change marks (ChangeGutter), folds.
constexpr int foldMargin = 2;

// After those of the find bar, snippets and completion choices.
constexpr int errorIndicator = 13;
constexpr int warningIndicator = 14;
// As long as the documentation's hover (CodeIntelligence).
constexpr int hoverDelayMs = 600;
// The fold marks are drawn at twice their size, sharp on high density
// screens.
constexpr int foldMarkSize = 14;
constexpr qreal foldMarkRatio = 2;
// How far up the active indentation guide looks for its block's header.
constexpr int guideSearchLines = 2000;

// Navigation shortcuts (see CodeNavigator) until MainWindow passes the
// menu's shortcuts. Shared by every editor: the keymap is the
// application's.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
QList<int> reservedKeys = {(Qt::CTRL | Qt::Key_B).toCombined(), (Qt::ALT | Qt::Key_Left).toCombined(),
                           (Qt::ALT | Qt::Key_Right).toCombined()};
// The editors alive, which a keymap change reaches.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::vector<EditorWidget *> liveEditors;

// A thin chevron pointing right (a folded block) or down (an open one).
QImage foldChevron(const QColor &color, bool open)
{
    QImage image(QSize(foldMarkSize, foldMarkSize) * foldMarkRatio, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(foldMarkRatio, foldMarkRatio);
    painter.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const qreal center = foldMarkSize / 2.0;
    using Chevron = std::array<QPointF, 3>;
    const Chevron right = {QPointF(center - 1.5, center - 3.5), QPointF(center + 2, center),
                           QPointF(center - 1.5, center + 3.5)};
    const Chevron down = {QPointF(center - 3.5, center - 1.5), QPointF(center, center + 2),
                          QPointF(center + 3.5, center - 1.5)};
    const Chevron &chevron = open ? down : right;
    painter.drawPolyline(chevron.data(), int(chevron.size()));
    return image;
}

bool isBlankLine(const QsciScintilla &editor, int line)
{
    return editor.SendScintilla(QsciScintillaBase::SCI_GETLINEINDENTPOSITION, line) ==
           editor.SendScintilla(QsciScintillaBase::SCI_GETLINEENDPOSITION, line);
}

int lineIndentation(const QsciScintilla &editor, int line)
{
    return static_cast<int>(editor.SendScintilla(QsciScintillaBase::SCI_GETLINEINDENTATION, line));
}

// The nearest non-blank line from `line` in the direction `step`; -1 if
// there is none.
int nonBlankLine(const QsciScintilla &editor, int line, int step)
{
    const int count = editor.lines();
    for (; line >= 0 && line < count; line += step) {
        if (!isBlankLine(editor, line))
            return line;
    }
    return -1;
}

// The indentation of `line`; for a blank line, that of the deeper of its
// non-blank neighbors.
int effectiveIndentation(const QsciScintilla &editor, int line, int previous, int next)
{
    if (!isBlankLine(editor, line))
        return lineIndentation(editor, line);
    const int previousIndentation = previous >= 0 ? lineIndentation(editor, previous) : 0;
    const int nextIndentation = next >= 0 ? lineIndentation(editor, next) : 0;
    return std::max(previousIndentation, nextIndentation);
}

bool startsWithClosingBracket(const QsciScintilla &editor, int line)
{
    const auto first = editor.SendScintilla(QsciScintillaBase::SCI_GETCHARAT,
                                            editor.SendScintilla(QsciScintillaBase::SCI_GETLINEINDENTPOSITION, line));
    return first == '}' || first == ')' || first == ']';
}

// The indentation of the nearest line above `line` less indented than
// `indentation`: the header of the block holding `line`. 0 if there is none.
int enclosingIndentation(const QsciScintilla &editor, int line, int indentation)
{
    for (int above = line - 1; above >= std::max(0, line - guideSearchLines); --above) {
        if (!isBlankLine(editor, above) && lineIndentation(editor, above) < indentation)
            return lineIndentation(editor, above);
    }
    return 0;
}

// Where a guide is drawn: its column, and a line of its block.
struct GuideAnchor {
    int column = 0;
    int inside = 0;
};

// On a header the block it opens, on a closing bracket the block it closes,
// elsewhere the block holding `line`.
GuideAnchor guideAnchor(const QsciScintilla &editor, int line)
{
    const int previous = nonBlankLine(editor, line - 1, -1);
    const int next = nonBlankLine(editor, line + 1, 1);
    const int indentation = effectiveIndentation(editor, line, previous, next);
    const bool opensBlock = next >= 0 && lineIndentation(editor, next) > indentation;
    if (opensBlock)
        return {.column = indentation, .inside = next};
    const bool followsDeeperLine = previous >= 0 && lineIndentation(editor, previous) > indentation;
    const bool closesBlock = !isBlankLine(editor, line) && followsDeeperLine && startsWithClosingBracket(editor, line);
    if (closesBlock)
        return {.column = indentation, .inside = previous};
    return {.column = enclosingIndentation(editor, line, indentation), .inside = line};
}

// The guide of the block around `anchor.inside`: the lines indented deeper
// than its header.
EditorWidget::ActiveGuide blockGuide(const QsciScintilla &editor, const GuideAnchor &anchor)
{
    const auto inBlock = [&](int other) { return lineIndentation(editor, other) > anchor.column; };
    int first = nonBlankLine(editor, anchor.inside, isBlankLine(editor, anchor.inside) ? -1 : 1);
    if (first < 0 || !inBlock(first))
        return {};
    int last = first;
    for (int above = nonBlankLine(editor, first - 1, -1);
         above >= 0 && inBlock(above) && first - above < guideSearchLines; above = nonBlankLine(editor, above - 1, -1))
        first = above;
    for (int below = nonBlankLine(editor, last + 1, 1); below >= 0 && inBlock(below) && below - last < guideSearchLines;
         below = nonBlankLine(editor, below + 1, 1))
        last = below;
    return {anchor.column, first, last};
}

// The indentation guide of the block holding `line`, at the indentation of
// the block's header. On a header the block it opens, on a closing bracket
// the block it closes; a blank line belongs to the deeper of its neighbors.
// None (column 0) for a block at the top level: Scintilla draws no guide
// there.
EditorWidget::ActiveGuide activeGuide(const QsciScintilla &editor, int line)
{
    const GuideAnchor anchor = guideAnchor(editor, line);
    if (anchor.column <= 0)
        return {};
    return blockGuide(editor, anchor);
}

QsciScintilla::EolMode toEolMode(LineEnding lineEnding)
{
    switch (lineEnding) {
    case LineEnding::CrLf:
        return QsciScintilla::EolWindows;
    case LineEnding::Cr:
        return QsciScintilla::EolMac;
    case LineEnding::Lf:
        break;
    }
    return QsciScintilla::EolUnix;
}

} // namespace

EditorWidget::EditorWidget(const Theme &theme, QWidget *parent)
    : QsciScintilla(parent)
    , theme_(theme)
    , changeGutter_(new ChangeGutter(this))
    , symbolOccurrences_(new SymbolOccurrences(this, theme))
    , mergeConflicts_(new MergeConflicts(this, theme))
{
    configureEditing();
    installHighlighter(LanguageRegistry::plainText);
    setOptions(options_);
    connect(this, &QsciScintilla::linesChanged, this, &EditorWidget::updateLineNumberMargin);
    connect(this, &QsciScintilla::cursorPositionChanged, this, &EditorWidget::revealCursorLine);
    // A click on a fold mark can hide the cursor's line: the cursor goes to
    // the fold's header, so that typing never edits hidden text. Queued,
    // because QScintilla folds after this signal.
    connect(this, &QsciScintillaBase::SCN_DWELLSTART, this,
            [this](int position, int x, int y) { showHoverInfo(position, QPoint(x, y)); });
    connect(this, &QsciScintillaBase::SCN_MARGINCLICK, this, [this](int, int, int margin) {
        if (margin == foldMargin)
            QTimer::singleShot(0, this, &EditorWidget::moveCursorOutOfFolds);
    });
    // On every move of the caret, edit and scroll.
    connect(this, &QsciScintillaBase::SCN_UPDATEUI, this, &EditorWidget::followCaretLine);
    liveEditors.push_back(this);
}

EditorWidget::~EditorWidget()
{
    std::erase(liveEditors, this);
    // The lexer belongs to the highlighter: QScintilla must not touch it anymore.
    setLexer(nullptr);
}

void EditorWidget::configureEditing()
{
    setUtf8(true);
    // Keeps the layout of every visible line (not only the caret's):
    // repainting while typing or scrolling, wrapped lines especially, does
    // not lay the text out again.
    SendScintilla(SCI_SETLAYOUTCACHE, SC_CACHE_PAGE);
    setMarginType(0, QsciScintilla::NumberMargin);
    setMarginLineNumbers(0, true);
    setMarginWidth(1, 0);
    setCaretLineVisible(true);
    setBraceMatching(QsciScintilla::SloppyBraceMatch);
    setAutoIndent(true);
    setIndentationsUseTabs(false);
    setTabWidth(4);
    setIndentationGuides(true);
    setBackspaceUnindents(true);
    setScrollWidth(1);
    setScrollWidthTracking(true);
    updateLineNumberMargin();

    for (const QsciCommand *command : standardCommands()->commands())
        defaultCommandKeys_.append({command->key(), command->alternateKey()});
    releaseReservedKeys();
}

void EditorWidget::setReservedKeys(const QList<QKeySequence> &shortcuts)
{
    reservedKeys.clear();
    for (const QKeySequence &shortcut : shortcuts) {
        // For "Ctrl+K, Ctrl+O", QScintilla must leave Ctrl+K.
        if (!shortcut.isEmpty())
            reservedKeys.append(shortcut[0].toCombined());
    }
    for (EditorWidget *editor : liveEditors)
        editor->releaseReservedKeys();
}

void EditorWidget::releaseReservedKeys()
{
    const QList<QsciCommand *> commands = standardCommands()->commands();
    for (int i = 0; i < commands.size() && i < defaultCommandKeys_.size(); ++i) {
        const auto [key, alternateKey] = defaultCommandKeys_[i];
        commands[i]->setKey(reservedKeys.contains(key) ? 0 : key);
        commands[i]->setAlternateKey(reservedKeys.contains(alternateKey) ? 0 : alternateKey);
    }
}

void EditorWidget::setOptions(const EditorOptions &options)
{
    if (!options.codeFolding && options_.codeFolding)
        foldEverything(false); // no hidden lines without the margin to show them
    options_ = options;
    setFolding(options.codeFolding ? QsciScintilla::PlainFoldStyle : QsciScintilla::NoFoldStyle, foldMargin);
    setWrapMode(options.wrapLines ? QsciScintilla::WrapWord : QsciScintilla::WrapNone);
    setWrapIndentMode(QsciScintilla::WrapIndentIndented);
    setWhitespaceVisibility(options.showWhitespace ? QsciScintilla::WsVisible : QsciScintilla::WsInvisible);
    applyFoldColors();
}

void EditorWidget::applyFoldColors()
{
    // QScintilla's fold marks are a black and white plus and minus:
    // chevrons in the margin's colors instead, brighter on a folded block.
    setFoldMarginColors(theme_.marginBackground, theme_.marginBackground);
    if (options_.codeFolding) {
        SendScintilla(SCI_RGBAIMAGESETSCALE, qRound(100 * foldMarkRatio));
        markerDefine(foldChevron(theme_.mutedText, false), SC_MARKNUM_FOLDER);
        SendScintilla(SCI_RGBAIMAGESETSCALE, 100);
        showOpenFoldMarks(openFoldMarksShown_);
    }
    setWhitespaceForegroundColor(theme_.marginForeground);
}

void EditorWidget::showOpenFoldMarks(bool shown)
{
    openFoldMarksShown_ = shown;
    if (!options_.codeFolding)
        return;
    if (!shown) {
        SendScintilla(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_EMPTY);
        return;
    }
    SendScintilla(SCI_RGBAIMAGESETSCALE, qRound(100 * foldMarkRatio));
    markerDefine(foldChevron(theme_.marginForeground, true), SC_MARKNUM_FOLDEROPEN);
    SendScintilla(SCI_RGBAIMAGESETSCALE, 100);
}

void EditorWidget::foldBlock(bool collapse)
{
    if (!options_.codeFolding)
        return;
    highlighter_->flush();
    int line = 0;
    int index = 0;
    getCursorPosition(&line, &index);
    const bool isHeader = SendScintilla(SCI_GETFOLDLEVEL, line) & SC_FOLDLEVELHEADERFLAG;

    if (!collapse) {
        if (isHeader && !SendScintilla(SCI_GETFOLDEXPANDED, line))
            SendScintilla(SCI_FOLDLINE, line, SC_FOLDACTION_EXPAND);
        return;
    }
    // The block that starts on the cursor's line, else the one around it;
    // when it is already folded, the one around that.
    int header = isHeader ? line : static_cast<int>(SendScintilla(SCI_GETFOLDPARENT, line));
    if (header >= 0 && !SendScintilla(SCI_GETFOLDEXPANDED, header))
        header = static_cast<int>(SendScintilla(SCI_GETFOLDPARENT, header));
    if (header < 0)
        return;
    SendScintilla(SCI_FOLDLINE, header, SC_FOLDACTION_CONTRACT);
    moveCursorOutOfFolds();
}

void EditorWidget::foldEverything(bool collapse)
{
    highlighter_->flush();
    SendScintilla(SCI_FOLDALL, collapse ? SC_FOLDACTION_CONTRACT : SC_FOLDACTION_EXPAND);
    moveCursorOutOfFolds();
}

void EditorWidget::moveCursorOutOfFolds()
{
    int line = 0;
    int index = 0;
    getCursorPosition(&line, &index);
    int visible = line;
    while (visible > 0 && !SendScintilla(SCI_GETLINEVISIBLE, visible)) {
        const int parent = static_cast<int>(SendScintilla(SCI_GETFOLDPARENT, visible));
        visible = parent >= 0 ? parent : visible - 1;
    }
    if (visible != line)
        SendScintilla(SCI_GOTOPOS, SendScintilla(SCI_GETLINEENDPOSITION, visible));
}

void EditorWidget::revealCursorLine(int line)
{
    if (!SendScintilla(SCI_GETLINEVISIBLE, line))
        SendScintilla(SCI_ENSUREVISIBLE, line);
}

void EditorWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::ControlModifier) {
        const QPoint point = event->position().toPoint();
        const long position = SendScintilla(SCI_POSITIONFROMPOINTCLOSE, point.x(), point.y());
        if (position >= 0) {
            SendScintilla(SCI_GOTOPOS, position);
            emit definitionRequested();
            return;
        }
    }
    QsciScintilla::mousePressEvent(event);
}

void EditorWidget::contextMenuEvent(QContextMenuEvent *event)
{
    // A right click outside the selection moves the cursor there first, as
    // in other editors: the file actions are about what was clicked.
    if (event->reason() == QContextMenuEvent::Mouse) {
        const long position = SendScintilla(SCI_POSITIONFROMPOINTCLOSE, event->pos().x(), event->pos().y());
        const long selectionStart = SendScintilla(SCI_GETSELECTIONSTART);
        const long selectionEnd = SendScintilla(SCI_GETSELECTIONEND);
        const bool insideSelection = position >= selectionStart && position <= selectionEnd;
        if (position >= 0 && !insideSelection)
            SendScintilla(SCI_GOTOPOS, position);
    }

    const QPointer<EditorWidget> guard(this); // an action may close the file
    isContextMenuShown_ = true;
    QToolTip::hideText();
    ContextMenus::exec(*createContextMenu(), *this, event->globalPos());
    if (guard)
        isContextMenuShown_ = false;
}

std::unique_ptr<QMenu> EditorWidget::createContextMenu()
{
    std::unique_ptr<QMenu> menu(createStandardContextMenu());
    removeUndoRedo(*menu);
    if (!filePath_.isEmpty() && !fileActions_.isEmpty()) {
        QAction *first = menu->actions().value(0);
        menu->insertActions(first, fileActions_);
        menu->insertSeparator(first);
    }
    addConflictActions(*menu);
    return menu;
}

void EditorWidget::addConflictActions(QMenu &menu)
{
    using ConflictMarkers::Resolution;
    mergeConflicts_->refreshNow();
    const std::optional<int> conflict = mergeConflicts_->conflictAt(static_cast<int>(SendScintilla(SCI_GETCURRENTPOS)));
    if (!conflict || isReadOnly())
        return;
    QAction *first = menu.actions().value(0);
    for (const auto &[text, resolution] : {std::pair{tr("Accept Current Change"), Resolution::Current},
                                           std::pair{tr("Accept Incoming Change"), Resolution::Incoming},
                                           std::pair{tr("Accept Both Changes"), Resolution::Both}}) {
        auto *action = new QAction(text, &menu);
        connect(action, &QAction::triggered, this,
                [this, index = *conflict, resolution] { mergeConflicts_->resolve(index, resolution); });
        menu.insertAction(first, action);
    }
    menu.insertSeparator(first);
}

void EditorWidget::removeUndoRedo(QMenu &menu)
{
    // Ctrl+Z and Ctrl+Shift+Z do it; QScintilla puts them first, with a
    // separator after them.
    for (QAction *action : menu.actions()) {
        const QString text = QString(action->text()).remove(QLatin1Char('&'));
        if (text == QLatin1String("Undo") || text == QLatin1String("Redo"))
            menu.removeAction(action);
    }
    const QList<QAction *> actions = menu.actions();
    if (!actions.isEmpty() && actions.first()->isSeparator())
        menu.removeAction(actions.first());
}

void EditorWidget::mouseMoveEvent(QMouseEvent *event)
{
    int marginsWidth = 0;
    for (int margin = 0; margin <= foldMargin; ++margin)
        marginsWidth += marginWidth(margin);
    const bool overMargins = event->position().x() < marginsWidth;
    if (overMargins != openFoldMarksShown_)
        showOpenFoldMarks(overMargins);
    QsciScintilla::mouseMoveEvent(event);
}

void EditorWidget::paintEvent(QPaintEvent *event)
{
    QsciScintilla::paintEvent(event);
    QPainter painter(viewport());
    paintActiveGuide(painter);
    paintCurrentLineNumber(painter, event->rect());
}

void EditorWidget::paintActiveGuide(QPainter &painter)
{
    // Scintilla lights a guide up only between two matching braces: the
    // caret's block's is drawn again over the faint ones, brighter.
    if (activeGuide_.column <= 0 || activeGuide_.lastLine >= lines())
        return;
    const long guidePosition = SendScintilla(SCI_FINDCOLUMN, activeGuide_.firstLine, activeGuide_.column);
    const int x = static_cast<int>(SendScintilla(SCI_POINTXFROMPOSITION, 0, guidePosition));
    int marginsWidth = 0;
    for (int margin = 0; margin <= foldMargin; ++margin)
        marginsWidth += marginWidth(margin);
    if (x < marginsWidth || x >= viewport()->width())
        return;
    const auto top = [this](int line) {
        return static_cast<int>(SendScintilla(SCI_POINTYFROMPOSITION, 0, SendScintilla(SCI_POSITIONFROMLINE, line)));
    };
    const int bottomLine = activeGuide_.lastLine;
    const int y1 = std::max(0, top(activeGuide_.firstLine));
    const int y2 = std::min(viewport()->height(),
                            top(bottomLine) + static_cast<int>(SendScintilla(SCI_TEXTHEIGHT, bottomLine)) *
                                                  static_cast<int>(SendScintilla(SCI_WRAPCOUNT, bottomLine)));
    if (y2 <= y1)
        return;
    painter.fillRect(QRect(x, y1, 1, y2 - y1), theme_.marginForeground);
}

void EditorWidget::paintCurrentLineNumber(QPainter &painter, const QRect &dirty)
{
    // Scintilla gives every line number the same color: the caret's is
    // painted again over it, brighter.
    const int line = numberedLine_;
    const int numbersWidth = marginWidth(0);
    if (line < 0 || line >= lines() || numbersWidth <= 0 || !SendScintilla(SCI_GETLINEVISIBLE, line))
        return;
    const int top =
        static_cast<int>(SendScintilla(SCI_POINTYFROMPOSITION, 0, SendScintilla(SCI_POSITIONFROMLINE, line)));
    const QRect row(0, top, numbersWidth, static_cast<int>(SendScintilla(SCI_TEXTHEIGHT, line)));
    if (!dirty.intersects(row))
        return;
    painter.fillRect(row, theme_.marginBackground);
    painter.setFont(theme_.font);
    painter.setPen(theme_.currentLineNumber);
    // Where Scintilla draws the numbers: right-aligned, 3 pixels from the
    // margin's edge, on the line's baseline.
    const QString number = QString::number(line + 1);
    const QFontMetrics metrics(theme_.font);
    const int baseline = top + static_cast<int>(SendScintilla(SCI_GETEXTRAASCENT)) + metrics.ascent();
    painter.drawText(numbersWidth - 3 - metrics.horizontalAdvance(number), baseline, number);
}

void EditorWidget::followCaretLine()
{
    int line = 0;
    int index = 0;
    getCursorPosition(&line, &index);
    const ActiveGuide guide = activeGuide(*this, line);
    if (guide != activeGuide_) {
        activeGuide_ = guide;
        viewport()->update();
    }
    if (line == numberedLine_)
        return;
    const int previous = std::exchange(numberedLine_, line);
    for (const int changed : {previous, line}) {
        if (changed < 0 || changed >= lines())
            continue;
        const int top =
            static_cast<int>(SendScintilla(SCI_POINTYFROMPOSITION, 0, SendScintilla(SCI_POSITIONFROMLINE, changed)));
        viewport()->update(0, top, marginWidth(0), static_cast<int>(SendScintilla(SCI_TEXTHEIGHT, changed)));
    }
}

void EditorWidget::leaveEvent(QEvent *event)
{
    if (openFoldMarksShown_)
        showOpenFoldMarks(false);
    QsciScintilla::leaveEvent(event);
}

void EditorWidget::installHighlighter(const QString &languageId)
{
    setLexer(nullptr);
    highlighter_ = HighlighterFactory::create(languageId);
    highlighter_->attach(this);
    applyTheme(theme_);
    if (languageId != languageId_) {
        languageId_ = languageId;
        emit languageChanged();
    }
}

bool EditorWidget::load(const QString &path, QString *error)
{
    const std::optional<DecodedText> decoded = TextFile::load(path, error);
    if (!decoded)
        return false;

    filePath_ = path;
    format_ = decoded->format;
    setText(decoded->text);
    setEolMode(toEolMode(format_.lineEnding));
    SendScintilla(SCI_EMPTYUNDOBUFFER);
    setModified(false);
    installHighlighter(LanguageRegistry::languageForFile(path));
    changeGutter_->reloadBase();
    emit formatChanged();
    return true;
}

bool EditorWidget::save(QString *error)
{
    if (filePath_.isEmpty() || isReadOnly())
        return true;
    const QString content = text();
    const TextFileFormat written = TextFile::formatFor(content, format_);
    if (!TextFile::save(filePath_, content, written, error))
        return false;
    setModified(false);
    if (written.isLatin1 != format_.isLatin1) {
        format_ = written;
        emit formatChanged();
    }
    return true;
}

bool EditorWidget::reloadFromDisk(QString *error)
{
    const std::optional<DecodedText> decoded = TextFile::load(filePath_, error);
    if (!decoded)
        return false;

    const int topLine = firstVisibleLine();
    int line = 0;
    int index = 0;
    getCursorPosition(&line, &index);

    if (decoded->format.lineEnding != format_.lineEnding)
        setEolMode(toEolMode(decoded->format.lineEnding));
    format_ = decoded->format;
    emit formatChanged();
    // Replace (rather than setText) so that Ctrl+Z restores the previous
    // text; a target with its length, as a selection stops at a NUL byte.
    const QByteArray bytes = decoded->text.toUtf8();
    SendScintilla(SCI_SETTARGETRANGE, 0UL, SendScintilla(SCI_GETLENGTH));
    SendScintilla(SCI_REPLACETARGET, uintptr_t(bytes.size()), bytes.constData());
    setModified(false);

    setCursorPosition(std::min(line, lines() - 1), index);
    setFirstVisibleLine(topLine);
    return true;
}

QByteArrayView EditorWidget::utf8Bytes() const
{
    const auto length = static_cast<qsizetype>(SendScintilla(SCI_GETLENGTH));
    return {static_cast<const char *>(SendScintillaPtrResult(SCI_GETCHARACTERPOINTER)), length};
}

QByteArray EditorWidget::utf8Range(int start, int end) const
{
    if (end <= start)
        return {};
    QByteArray bytes(end - start + 1, Qt::Uninitialized); // SCI_GETTEXTRANGE adds a NUL
    SendScintilla(SCI_GETTEXTRANGE, static_cast<long>(start), static_cast<long>(end), bytes.data());
    bytes.chop(1);
    return bytes;
}

bool EditorWidget::matchesDisk() const
{
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return file.readAll() == TextFile::encode(text(), format_);
}

bool EditorWidget::applyFormattedText(QString formatted)
{
    // Formatters may write "\n" whatever the file uses.
    formatted.replace(QLatin1String("\r\n"), QLatin1String("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (format_.lineEnding == LineEnding::CrLf)
        formatted.replace(QLatin1Char('\n'), QLatin1String("\r\n"));
    else if (format_.lineEnding == LineEnding::Cr)
        formatted.replace(QLatin1Char('\n'), QLatin1Char('\r'));

    const QByteArray before = text().toUtf8();
    const QByteArray after = formatted.toUtf8();
    if (before == after)
        return false;

    const int caret = static_cast<int>(SendScintilla(SCI_GETCURRENTPOS));
    const int anchor = static_cast<int>(SendScintilla(SCI_GETANCHOR));
    const int caretLine = static_cast<int>(SendScintilla(SCI_LINEFROMPOSITION, caret));
    const int linesAboveCaret = caretLine - firstVisibleLine();

    // Only the part that differs is replaced: the lines around it keep their
    // markers and the undo step stays small.
    qsizetype prefix = 0;
    const qsizetype shorter = std::min(before.size(), after.size());
    while (prefix < shorter && before[prefix] == after[prefix])
        ++prefix;
    while (prefix > 0 && (static_cast<unsigned char>(before[prefix]) & 0xC0) == 0x80)
        --prefix;
    qsizetype suffix = 0;
    while (suffix < shorter - prefix && before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix])
        ++suffix;
    while (suffix > 0 && (static_cast<unsigned char>(before[before.size() - suffix]) & 0xC0) == 0x80)
        --suffix;

    const QByteArray replacement = after.mid(prefix, after.size() - prefix - suffix);
    SendScintilla(SCI_BEGINUNDOACTION);
    SendScintilla(SCI_SETTARGETRANGE, static_cast<unsigned long>(prefix), static_cast<long>(before.size() - suffix));
    SendScintilla(SCI_REPLACETARGET, uintptr_t(replacement.size()), replacement.constData());
    SendScintilla(SCI_ENDUNDOACTION);

    SendScintilla(SCI_SETSEL, static_cast<unsigned long>(CodeFormatter::mapPosition(before, anchor, after)),
                  static_cast<long>(CodeFormatter::mapPosition(before, caret, after)));
    const int newCaretLine = static_cast<int>(SendScintilla(SCI_LINEFROMPOSITION, SendScintilla(SCI_GETCURRENTPOS)));
    setFirstVisibleLine(std::max(0, newCaretLine - linesAboveCaret));
    return true;
}

void EditorWidget::showReadOnlyText(const QString &text, const QString &languageId)
{
    setReadOnly(false);
    setText(text);
    setReadOnly(true);
    setModified(false);
    installHighlighter(languageId);
}

void EditorWidget::setLineEnding(LineEnding lineEnding)
{
    format_.lineEnding = lineEnding;
    setEolMode(toEolMode(lineEnding));
    if (!isReadOnly())
        convertEols(toEolMode(lineEnding));
    emit formatChanged();
}

void EditorWidget::setIndentationStyle(bool useTabs, int width)
{
    setIndentationsUseTabs(useTabs);
    setTabWidth(width);
    emit formatChanged();
}

void EditorWidget::showColumnLimit(int columnLimit)
{
    setEdgeMode(columnLimit > 0 ? QsciScintilla::EdgeLine : QsciScintilla::EdgeNone);
    setEdgeColumn(columnLimit);
}

void EditorWidget::applyTheme(const Theme &theme)
{
    theme_ = theme;
    highlighter_->applyTheme(theme);
    changeGutter_->applyTheme(theme);
    symbolOccurrences_->applyTheme(theme);
    mergeConflicts_->applyTheme(theme);

    // After the highlighter: a QScintilla lexer resets these colors.
    setCaretForegroundColor(theme.caret);
    setCaretLineBackgroundColor(theme.caretLine);
    setSelectionBackgroundColor(theme.selection);
    SendScintilla(SCI_SETSELFORE, false, 0L); // the selection keeps the syntax colors
    setMarginsBackgroundColor(theme.marginBackground);
    setMarginsForegroundColor(theme.marginForeground);
    setMarginsFont(theme.font);
    setMatchedBraceBackgroundColor(theme.braceMatch);
    setMatchedBraceForegroundColor(theme.foreground);
    setUnmatchedBraceForegroundColor(theme.error);
    setIndentationGuidesBackgroundColor(theme.background);
    setIndentationGuidesForegroundColor(theme.caretLine);
    setEdgeColor(theme.caretLine);
    ScintillaStyles::applyTypography(*this, theme.font, theme.lineHeight);
    SendScintilla(SCI_INDICSETFORE, errorIndicator, theme.error);
    SendScintilla(SCI_INDICSETFORE, warningIndicator, theme.warning);
    applyFoldColors();
    updateLineNumberMargin();
}

void EditorWidget::setDiagnostics(const Diagnostics &diagnostics)
{
    if (diagnostics == diagnostics_)
        return;
    diagnostics_ = diagnostics;
    const long length = SendScintilla(SCI_GETLENGTH);
    for (const auto &[indicator, color] :
         {std::pair{errorIndicator, theme_.error}, std::pair{warningIndicator, theme_.warning}}) {
        SendScintilla(SCI_INDICSETSTYLE, indicator, INDIC_SQUIGGLEPIXMAP);
        SendScintilla(SCI_INDICSETFORE, indicator, color);
        SendScintilla(SCI_INDICSETUNDER, indicator, true);
        SendScintilla(SCI_SETINDICATORCURRENT, indicator);
        SendScintilla(SCI_INDICATORCLEARRANGE, 0, length);
    }
    for (const Diagnostic &diagnostic : diagnostics_) {
        const long start = std::clamp<long>(diagnostic.start, 0, length);
        const long end = std::clamp<long>(diagnostic.end, start, length);
        SendScintilla(SCI_SETINDICATORCURRENT,
                      diagnostic.severity == Diagnostic::Severity::Error ? errorIndicator : warningIndicator);
        SendScintilla(SCI_INDICATORFILLRANGE, start, end - start);
    }
    if (!diagnostics_.isEmpty())
        enableDwell();
}

void EditorWidget::setHoverHelp(std::function<QString(int)> help)
{
    hoverHelp_ = std::move(help);
    if (hoverHelp_)
        enableDwell();
}

void EditorWidget::enableDwell()
{
    if (SendScintilla(SCI_GETMOUSEDWELLTIME) >= SC_TIME_FOREVER)
        SendScintilla(SCI_SETMOUSEDWELLTIME, hoverDelayMs);
}

void EditorWidget::setToolTipsSuspended(bool suspended)
{
    toolTipsSuspended_ = suspended;
    if (suspended)
        QToolTip::hideText();
}

void EditorWidget::showHoverInfo(int position, const QPoint &point)
{
    if (position < 0 || !isActiveWindow() || areToolTipsSuspended())
        return;
    QStringList messages;
    for (const Diagnostic &diagnostic : std::as_const(diagnostics_)) {
        if (position >= diagnostic.start && position < diagnostic.end)
            messages.append(diagnostic.message.toHtmlEscaped());
    }
    if (messages.isEmpty() && hoverHelp_) {
        const QString help = hoverHelp_(position);
        if (!help.isEmpty()) {
            // The first line in bold: what the text is about.
            const qsizetype newline = help.indexOf(QLatin1Char('\n'));
            messages.append(newline < 0 ? help.toHtmlEscaped()
                                        : QStringLiteral("<b>%1</b><br>%2")
                                              .arg(help.left(newline).toHtmlEscaped(),
                                                   help.mid(newline + 1)
                                                       .toHtmlEscaped()
                                                       .replace(QLatin1Char('\n'), QStringLiteral("<br>"))));
        }
    }
    if (messages.isEmpty())
        return;
    QToolTip::showText(viewport()->mapToGlobal(point + QPoint(0, 12)),
                       QStringLiteral("<p style=\"white-space: pre-wrap; margin: 0\">%1</p>")
                           .arg(messages.join(QStringLiteral("<br>"))),
                       viewport());
}

void EditorWidget::updateLineNumberMargin()
{
    const QString widest = QString(QString::number(lines()).size() + 1, QLatin1Char('9'));
    setMarginWidth(0, widest);
}

void EditorWidget::wheelEvent(QWheelEvent *event)
{
    // QScintilla zooms with the Control key on macOS; Kodamiko, as in its trees,
    // with Command there (Qt's ControlModifier) and Ctrl elsewhere.
    if (!(event->modifiers() & Qt::ControlModifier)) {
        QsciScintillaBase::wheelEvent(event);
        return;
    }
    const int delta = event->angleDelta().y();
    if (delta > 0)
        zoomIn();
    else if (delta < 0)
        zoomOut();
}
