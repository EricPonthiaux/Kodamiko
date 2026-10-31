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

bool isBlankLine(const ScintillaView &editor, int line)
{
    return editor.send(SCI_GETLINEINDENTPOSITION, line) == editor.send(SCI_GETLINEENDPOSITION, line);
}

int lineIndentation(const ScintillaView &editor, int line)
{
    return static_cast<int>(editor.send(SCI_GETLINEINDENTATION, line));
}

// The nearest non-blank line from `line` in the direction `step`; -1 if
// there is none.
int nonBlankLine(const ScintillaView &editor, int line, int step)
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
int effectiveIndentation(const ScintillaView &editor, int line, int previous, int next)
{
    if (!isBlankLine(editor, line))
        return lineIndentation(editor, line);
    const int previousIndentation = previous >= 0 ? lineIndentation(editor, previous) : 0;
    const int nextIndentation = next >= 0 ? lineIndentation(editor, next) : 0;
    return std::max(previousIndentation, nextIndentation);
}

bool startsWithClosingBracket(const ScintillaView &editor, int line)
{
    const auto first = editor.send(SCI_GETCHARAT, editor.send(SCI_GETLINEINDENTPOSITION, line));
    return first == '}' || first == ')' || first == ']';
}

// The indentation of the nearest line above `line` less indented than
// `indentation`: the header of the block holding `line`. 0 if there is none.
int enclosingIndentation(const ScintillaView &editor, int line, int indentation)
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
GuideAnchor guideAnchor(const ScintillaView &editor, int line)
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
EditorWidget::ActiveGuide blockGuide(const ScintillaView &editor, const GuideAnchor &anchor)
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
EditorWidget::ActiveGuide activeGuide(const ScintillaView &editor, int line)
{
    const GuideAnchor anchor = guideAnchor(editor, line);
    if (anchor.column <= 0)
        return {};
    return blockGuide(editor, anchor);
}

int toEolMode(LineEnding lineEnding)
{
    switch (lineEnding) {
    case LineEnding::CrLf:
        return SC_EOL_CRLF;
    case LineEnding::Cr:
        return SC_EOL_CR;
    case LineEnding::Lf:
        break;
    }
    return SC_EOL_LF;
}

} // namespace

EditorWidget::EditorWidget(const Theme &theme, QWidget *parent)
    : ScintillaView(parent)
    , theme_(theme)
    , changeGutter_(new ChangeGutter(this))
    , symbolOccurrences_(new SymbolOccurrences(this, theme))
    , mergeConflicts_(new MergeConflicts(this, theme))
{
    configureEditing();
    installHighlighter(LanguageRegistry::plainText);
    setOptions(options_);
    connect(this, &ScintillaView::linesChanged, this, &EditorWidget::updateLineNumberMargin);
    connect(this, &ScintillaView::cursorPositionChanged, this, &EditorWidget::revealCursorLine);
    connect(this, &ScintillaView::dwellStarted, this, &EditorWidget::showHoverInfo);
    // A click on a fold mark can hide the cursor's line: the cursor goes to
    // the fold's header, so that typing never edits hidden text. Queued, so
    // that the block is folded first.
    connect(this, &ScintillaEditBase::marginClicked, this, [this](Scintilla::Position, Scintilla::KeyMod, int margin) {
        if (margin == foldMargin)
            QTimer::singleShot(0, this, &EditorWidget::moveCursorOutOfFolds);
    });
    // On every move of the caret, edit and scroll.
    connect(this, &ScintillaEditBase::updateUi, this, &EditorWidget::followCaretLine);
}

EditorWidget::~EditorWidget() = default;

void EditorWidget::configureEditing()
{
    // The style's sunken frame drew a dark line along the top and the left:
    // under the current tab, which opens onto the text.
    setFrameShape(QFrame::NoFrame);
    // Keeps the layout of every visible line (not only the caret's):
    // repainting while typing or scrolling, wrapped lines especially, does
    // not lay the text out again.
    send(SCI_SETLAYOUTCACHE, SC_CACHE_PAGE);
    setMarginType(0, SC_MARGIN_NUMBER);
    setMarginWidth(1, 0);
    setCaretLineVisible(true);
    setBraceMatching(true);
    setAutoIndent(true);
    send(SCI_SETUSETABS, false);
    send(SCI_SETTABWIDTH, 4);
    send(SCI_SETINDENTATIONGUIDES, SC_IV_REAL);
    send(SCI_SETBACKSPACEUNINDENTS, true);
    send(SCI_SETSCROLLWIDTH, 1);
    send(SCI_SETSCROLLWIDTHTRACKING, true);
    updateLineNumberMargin();
}

void EditorWidget::setOptions(const EditorOptions &options)
{
    if (!options.codeFolding && options_.codeFolding)
        foldEverything(false); // no hidden lines without the margin to show them
    options_ = options;
    setFolding(options.codeFolding, foldMargin);
    send(SCI_SETWRAPMODE, options.wrapLines ? SC_WRAP_WORD : SC_WRAP_NONE);
    send(SCI_SETWRAPINDENTMODE, SC_WRAPINDENT_INDENT);
    send(SCI_SETVIEWWS, options.showWhitespace ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
    applyFoldColors();
}

void EditorWidget::applyFoldColors()
{
    // ScintillaView's fold marks are a black and white plus and minus:
    // chevrons in the margin's colors instead, brighter on a folded block.
    send(SCI_SETFOLDMARGINCOLOUR, true, scintillaColor(theme_.marginBackground));
    send(SCI_SETFOLDMARGINHICOLOUR, true, scintillaColor(theme_.marginBackground));
    if (options_.codeFolding) {
        send(SCI_RGBAIMAGESETSCALE, qRound(100 * foldMarkRatio));
        markerDefine(foldChevron(theme_.mutedText, false), SC_MARKNUM_FOLDER);
        send(SCI_RGBAIMAGESETSCALE, 100);
        showOpenFoldMarks(openFoldMarksShown_);
    }
    send(SCI_SETWHITESPACEFORE, true, scintillaColor(theme_.marginForeground));
}

void EditorWidget::showOpenFoldMarks(bool shown)
{
    openFoldMarksShown_ = shown;
    if (!options_.codeFolding)
        return;
    if (!shown) {
        send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_EMPTY);
        return;
    }
    send(SCI_RGBAIMAGESETSCALE, qRound(100 * foldMarkRatio));
    markerDefine(foldChevron(theme_.marginForeground, true), SC_MARKNUM_FOLDEROPEN);
    send(SCI_RGBAIMAGESETSCALE, 100);
}

void EditorWidget::foldBlock(bool collapse)
{
    if (!options_.codeFolding)
        return;
    highlighter_->flush();
    int line = 0;
    int index = 0;
    getCursorPosition(&line, &index);
    const bool isHeader = send(SCI_GETFOLDLEVEL, line) & SC_FOLDLEVELHEADERFLAG;

    if (!collapse) {
        if (isHeader && !send(SCI_GETFOLDEXPANDED, line))
            send(SCI_FOLDLINE, line, SC_FOLDACTION_EXPAND);
        return;
    }
    // The block that starts on the cursor's line, else the one around it;
    // when it is already folded, the one around that.
    int header = isHeader ? line : static_cast<int>(send(SCI_GETFOLDPARENT, line));
    if (header >= 0 && !send(SCI_GETFOLDEXPANDED, header))
        header = static_cast<int>(send(SCI_GETFOLDPARENT, header));
    if (header < 0)
        return;
    send(SCI_FOLDLINE, header, SC_FOLDACTION_CONTRACT);
    moveCursorOutOfFolds();
}

void EditorWidget::foldEverything(bool collapse)
{
    highlighter_->flush();
    send(SCI_FOLDALL, collapse ? SC_FOLDACTION_CONTRACT : SC_FOLDACTION_EXPAND);
    moveCursorOutOfFolds();
}

void EditorWidget::moveCursorOutOfFolds()
{
    int line = 0;
    int index = 0;
    getCursorPosition(&line, &index);
    int visible = line;
    while (visible > 0 && !send(SCI_GETLINEVISIBLE, visible)) {
        const int parent = static_cast<int>(send(SCI_GETFOLDPARENT, visible));
        visible = parent >= 0 ? parent : visible - 1;
    }
    if (visible != line)
        send(SCI_GOTOPOS, send(SCI_GETLINEENDPOSITION, visible));
}

void EditorWidget::revealCursorLine(int line)
{
    if (!send(SCI_GETLINEVISIBLE, line))
        send(SCI_ENSUREVISIBLE, line);
}

void EditorWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::ControlModifier) {
        const QPoint point = event->position().toPoint();
        const long position = send(SCI_POSITIONFROMPOINTCLOSE, point.x(), point.y());
        if (position >= 0) {
            send(SCI_GOTOPOS, position);
            emit definitionRequested();
            return;
        }
    }
    ScintillaView::mousePressEvent(event);
}

void EditorWidget::contextMenuEvent(QContextMenuEvent *event)
{
    // A right click outside the selection moves the cursor there first, as
    // in other editors: the file actions are about what was clicked.
    if (event->reason() == QContextMenuEvent::Mouse) {
        const long position = send(SCI_POSITIONFROMPOINTCLOSE, event->pos().x(), event->pos().y());
        const long selectionStart = send(SCI_GETSELECTIONSTART);
        const long selectionEnd = send(SCI_GETSELECTIONEND);
        const bool insideSelection = position >= selectionStart && position <= selectionEnd;
        if (position >= 0 && !insideSelection)
            send(SCI_GOTOPOS, position);
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
    std::unique_ptr<QMenu> menu = createStandardContextMenu();
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
    const std::optional<int> conflict = mergeConflicts_->conflictAt(static_cast<int>(send(SCI_GETCURRENTPOS)));
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
    // Ctrl+Z and Ctrl+Shift+Z do it; ScintillaView puts them first, with a
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
    ScintillaView::mouseMoveEvent(event);
}

void EditorWidget::paintEvent(QPaintEvent *event)
{
    ScintillaView::paintEvent(event);
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
    const long guidePosition = send(SCI_FINDCOLUMN, activeGuide_.firstLine, activeGuide_.column);
    const int x = static_cast<int>(send(SCI_POINTXFROMPOSITION, 0, guidePosition));
    int marginsWidth = 0;
    for (int margin = 0; margin <= foldMargin; ++margin)
        marginsWidth += marginWidth(margin);
    if (x < marginsWidth || x >= viewport()->width())
        return;
    const auto top = [this](int line) {
        return static_cast<int>(send(SCI_POINTYFROMPOSITION, 0, send(SCI_POSITIONFROMLINE, line)));
    };
    const int bottomLine = activeGuide_.lastLine;
    const int y1 = std::max(0, top(activeGuide_.firstLine));
    const int y2 =
        std::min(viewport()->height(), top(bottomLine) + static_cast<int>(send(SCI_TEXTHEIGHT, bottomLine)) *
                                                             static_cast<int>(send(SCI_WRAPCOUNT, bottomLine)));
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
    if (line < 0 || line >= lines() || numbersWidth <= 0 || !send(SCI_GETLINEVISIBLE, line))
        return;
    const int top = static_cast<int>(send(SCI_POINTYFROMPOSITION, 0, send(SCI_POSITIONFROMLINE, line)));
    const QRect row(0, top, numbersWidth, static_cast<int>(send(SCI_TEXTHEIGHT, line)));
    if (!dirty.intersects(row))
        return;
    painter.fillRect(row, theme_.marginBackground);
    painter.setFont(theme_.font);
    painter.setPen(theme_.currentLineNumber);
    // Where Scintilla draws the numbers: right-aligned, 3 pixels from the
    // margin's edge, on the line's baseline.
    const QString number = QString::number(line + 1);
    const QFontMetrics metrics(theme_.font);
    const int baseline = top + static_cast<int>(send(SCI_GETEXTRAASCENT)) + metrics.ascent();
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
        const int top = static_cast<int>(send(SCI_POINTYFROMPOSITION, 0, send(SCI_POSITIONFROMLINE, changed)));
        viewport()->update(0, top, marginWidth(0), static_cast<int>(send(SCI_TEXTHEIGHT, changed)));
    }
}

void EditorWidget::leaveEvent(QEvent *event)
{
    if (openFoldMarksShown_)
        showOpenFoldMarks(false);
    ScintillaView::leaveEvent(event);
}

QString EditorWidget::styleCategory(int style) const
{
    return highlighter_->styleCategory(style, theme_);
}

void EditorWidget::installHighlighter(const QString &languageId)
{
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
    send(SCI_SETEOLMODE, toEolMode(format_.lineEnding));
    send(SCI_EMPTYUNDOBUFFER);
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
        send(SCI_SETEOLMODE, toEolMode(decoded->format.lineEnding));
    format_ = decoded->format;
    emit formatChanged();
    // Replace (rather than setText) so that Ctrl+Z restores the previous
    // text; a target with its length, as a selection stops at a NUL byte.
    const QByteArray bytes = decoded->text.toUtf8();
    send(SCI_SETTARGETRANGE, 0UL, send(SCI_GETLENGTH));
    sends(SCI_REPLACETARGET, uintptr_t(bytes.size()), bytes.constData());
    setModified(false);

    setCursorPosition(std::min(line, lines() - 1), index);
    setFirstVisibleLine(topLine);
    return true;
}

QByteArrayView EditorWidget::utf8Bytes() const
{
    const auto length = static_cast<qsizetype>(send(SCI_GETLENGTH));
    return {characterPointer(), length};
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

    const int caret = static_cast<int>(send(SCI_GETCURRENTPOS));
    const int anchor = static_cast<int>(send(SCI_GETANCHOR));
    const int caretLine = static_cast<int>(send(SCI_LINEFROMPOSITION, caret));
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
    send(SCI_BEGINUNDOACTION);
    send(SCI_SETTARGETRANGE, static_cast<unsigned long>(prefix), static_cast<long>(before.size() - suffix));
    sends(SCI_REPLACETARGET, uintptr_t(replacement.size()), replacement.constData());
    send(SCI_ENDUNDOACTION);

    send(SCI_SETSEL, static_cast<unsigned long>(CodeFormatter::mapPosition(before, anchor, after)),
         static_cast<long>(CodeFormatter::mapPosition(before, caret, after)));
    const int newCaretLine = static_cast<int>(send(SCI_LINEFROMPOSITION, send(SCI_GETCURRENTPOS)));
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
    send(SCI_SETEOLMODE, toEolMode(lineEnding));
    if (!isReadOnly())
        send(SCI_CONVERTEOLS, toEolMode(lineEnding));
    emit formatChanged();
}

void EditorWidget::setIndentationStyle(bool useTabs, int width)
{
    send(SCI_SETUSETABS, useTabs);
    send(SCI_SETTABWIDTH, width);
    emit formatChanged();
}

void EditorWidget::showColumnLimit(int columnLimit)
{
    send(SCI_SETEDGEMODE, columnLimit > 0 ? EDGE_LINE : EDGE_NONE);
    send(SCI_SETEDGECOLUMN, columnLimit);
}

void EditorWidget::applyTheme(const Theme &theme)
{
    theme_ = theme;
    highlighter_->applyTheme(theme);
    changeGutter_->applyTheme(theme);
    symbolOccurrences_->applyTheme(theme);
    mergeConflicts_->applyTheme(theme);

    // After the highlighter: its default style resets the special styles.
    ScintillaStyles::applyEditorColors(*this, theme);
    ScintillaStyles::applyTypography(*this, theme.font, theme.lineHeight);
    send(SCI_INDICSETFORE, errorIndicator, scintillaColor(theme.error));
    send(SCI_INDICSETFORE, warningIndicator, scintillaColor(theme.warning));
    applyFoldColors();
    updateLineNumberMargin();
}

void EditorWidget::setDiagnostics(const Diagnostics &diagnostics)
{
    if (diagnostics == diagnostics_)
        return;
    diagnostics_ = diagnostics;
    const long length = send(SCI_GETLENGTH);
    for (const auto &[indicator, color] :
         {std::pair{errorIndicator, theme_.error}, std::pair{warningIndicator, theme_.warning}}) {
        send(SCI_INDICSETSTYLE, indicator, INDIC_SQUIGGLEPIXMAP);
        send(SCI_INDICSETFORE, indicator, scintillaColor(color));
        send(SCI_INDICSETUNDER, indicator, true);
        send(SCI_SETINDICATORCURRENT, indicator);
        send(SCI_INDICATORCLEARRANGE, 0, length);
    }
    for (const Diagnostic &diagnostic : diagnostics_) {
        const long start = std::clamp<long>(diagnostic.start, 0, length);
        const long end = std::clamp<long>(diagnostic.end, start, length);
        send(SCI_SETINDICATORCURRENT,
             diagnostic.severity == Diagnostic::Severity::Error ? errorIndicator : warningIndicator);
        send(SCI_INDICATORFILLRANGE, start, end - start);
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
    if (send(SCI_GETMOUSEDWELLTIME) >= SC_TIME_FOREVER)
        send(SCI_SETMOUSEDWELLTIME, hoverDelayMs);
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
    // Scintilla zooms with the Control key on macOS; Kodamiko, as in its trees,
    // with Command there (Qt's ControlModifier) and Ctrl elsewhere.
    if (!(event->modifiers() & Qt::ControlModifier)) {
        ScintillaView::wheelEvent(event);
        return;
    }
    const int delta = event->angleDelta().y();
    if (delta > 0)
        zoomIn();
    else if (delta < 0)
        zoomOut();
}
