// SPDX-License-Identifier: GPL-3.0-only

#include "DiffView.h"

#include "DiffStyles.h"
#include "DiffSyntaxHighlighter.h"
#include "syntax/ScintillaStyles.h"

#include <QFontMetrics>
#include <QPalette>
#include <QShortcut>
#include <QWheelEvent>

#include <algorithm>

using namespace DiffStyles;

namespace {

long utf8Length(const QString &text, int characters)
{
    return static_cast<long>(text.left(characters).toUtf8().size());
}

} // namespace

DiffView::DiffView(const Theme &theme, QWidget *parent)
    : QsciScintilla(parent)
    , theme_(theme)
{
    setUtf8(true);
    setReadOnly(true);
    setCaretLineVisible(false);
    setWrapMode(QsciScintilla::WrapNone);
    setScrollWidthTracking(true);
    setScrollWidth(1);
    setMarginType(numberMargin, QsciScintilla::TextMarginRightJustified);
    setMarginType(stripMargin, QsciScintilla::SymbolMargin);
    setMarginMarkerMask(numberMargin, 0);
    setMarginMarkerMask(stripMargin,
                        (1 << addedStrip) | (1 << removedStrip) | (1 << fileHeaderStrip) | (1 << hunkHeaderStrip));
    setMarginWidth(stripMargin, 3);
    for (int margin = 2; margin < 5; ++margin)
        setMarginWidth(margin, 0);
    SendScintilla(SCI_ANNOTATIONSETVISIBLE, long(ANNOTATION_BOXED));
    applyTheme(theme);
    addShortcuts();
    // Queued: the arrow's lines replace the text Scintilla is handling the
    // click in.
    connect(this, &QsciScintillaBase::SCN_HOTSPOTRELEASECLICK, this, &DiffView::onArrowClicked, Qt::QueuedConnection);
}

void DiffView::addShortcuts()
{
    const auto add = [this](const QKeySequence &keys, auto slot) {
        const auto *shortcut = new QShortcut(keys, this, nullptr, nullptr, Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, this, slot);
    };
    add(QKeySequence(Qt::Key_F7), [this] { goToChange(Direction::Next); });
    add(QKeySequence(Qt::SHIFT | Qt::Key_F7), [this] { goToChange(Direction::Previous); });
    add(QKeySequence(Qt::Key_F8), [this] { goToNote(Direction::Next); });
    add(QKeySequence(Qt::SHIFT | Qt::Key_F8), [this] { goToNote(Direction::Previous); });
    add(QKeySequence(Qt::ALT | Qt::Key_Down), [this] { goToFile(Direction::Next); });
    add(QKeySequence(Qt::ALT | Qt::Key_Up), [this] { goToFile(Direction::Previous); });
}

void DiffView::setFiles(const QList<DiffFile> &files)
{
    files_ = files;
    notes_.clear();
    summaries_.clear();
    for (const DiffFile &file : files)
        summaries_.append({file.path, file.status, file.patch.addedLines(), file.patch.removedLines()});
    layOut();
}

void DiffView::hideUnchangedLines()
{
    revealed_.clear();
}

void DiffView::revealLines(int line, Reveal reveal)
{
    if (line < 0 || line >= lines_.size())
        return;
    const DiffLine &separator = lines_[line];
    const LineRange part = DiffLayout::revealedPart(separator.hidden, reveal);
    if (part.count() == 0 || separator.location.file < 0)
        return;
    QSet<int> &revealed = revealed_[DiffLayout::revealKey(files_[separator.location.file])];
    for (int number = part.first; number <= part.last; ++number)
        revealed.insert(number);
    const int top = firstVisibleLine();
    layOut();
    setCursorPosition(std::min(line, int(lines_.size()) - 1), 0); // scrolls to the cursor: first
    setFirstVisibleLine(top);
}

void DiffView::onArrowClicked(int position)
{
    const int line = int(SendScintilla(SCI_LINEFROMPOSITION, position));
    if (line < 0 || line >= lines_.size())
        return;
    const DiffLine &separator = lines_[line];
    const long column = position - SendScintilla(SCI_POSITIONFROMLINE, line);
    for (const RevealArrow &arrow : separator.arrows) {
        const bool isOnArrow = column >= utf8Length(separator.text, arrow.position) &&
                               column < utf8Length(separator.text, arrow.position + 1);
        if (isOnArrow) {
            revealLines(line, arrow.reveal);
            return;
        }
    }
}

void DiffView::showLines(QList<DiffLine> lines)
{
    lines_ = std::move(lines);
    fileStarts_.clear();
    int largest = 0;
    for (int i = 0; i < lines_.size(); ++i) {
        if (lines_[i].kind == DiffLineKind::FileHeader)
            fileStarts_.append(i);
        largest = std::max({largest, lines_[i].oldNumber, lines_[i].newNumber});
    }
    numberWidth_ = std::max(3, static_cast<int>(QString::number(largest).size()));
    render();
}

void DiffView::showMessage(const QString &message)
{
    lines_.clear();
    fileStarts_.clear();
    files_.clear();
    notes_.clear();
    summaries_.clear();
    if (!message.isEmpty()) {
        DiffLine note;
        note.kind = DiffLineKind::Note;
        note.text = message;
        lines_.append(note);
    }
    render();
    placeOldSide();
    emit contentChanged();
}

DiffLocation DiffView::locate(int line) const
{
    return line >= 0 && line < lines_.size() ? lines_[line].location : DiffLocation{};
}

int DiffView::fileStart(int file) const
{
    return fileStarts_.value(file, 0);
}

int DiffView::hunkStart(int line) const
{
    const DiffLocation location = locate(line);
    if (location.hunk < 0)
        return -1;
    for (int i = line; i >= 0; --i) {
        if (lines_[i].kind == DiffLineKind::HunkHeader)
            return i;
    }
    return -1;
}

void DiffView::goToChange(Direction direction)
{
    if (const auto separator = nextLineOfKind(DiffLineKind::HunkHeader, direction))
        showAtTop(*separator);
}

void DiffView::goToFile(Direction direction)
{
    if (listPosition_) {
        emit fileRequested(direction);
        return;
    }
    if (const auto title = nextLineOfKind(DiffLineKind::FileHeader, direction))
        showAtTop(*title);
}

void DiffView::setListPosition(std::optional<ListPosition> position)
{
    listPosition_ = position;
    emit contentChanged();
}

int DiffView::searchStart() const
{
    int cursorLine = 0;
    int index = 0;
    getCursorPosition(&cursorLine, &index);
    const int top = int(SendScintilla(SCI_DOCLINEFROMVISIBLE, firstVisibleLine()));
    const int bottom =
        int(SendScintilla(SCI_DOCLINEFROMVISIBLE, firstVisibleLine() + SendScintilla(SCI_LINESONSCREEN)));
    const bool isCursorInView = cursorLine >= top && cursorLine < bottom;
    return isCursorInView ? cursorLine : top;
}

std::optional<int> DiffView::nextLineOfKind(DiffLineKind kind, Direction direction) const
{
    const int step = direction == Direction::Next ? 1 : -1;
    for (int i = searchStart() + step; i >= 0 && i < lines_.size(); i += step) {
        if (lines_[i].kind == kind)
            return i;
    }
    return std::nullopt;
}

void DiffView::showAtTop(int line)
{
    setCursorPosition(line, 0); // scrolls to the cursor: first
    scrollToTop(line);
}

void DiffView::scrollToTop(int line)
{
    // The notes' boxes above take rows of their own.
    setFirstVisibleLine(int(SendScintilla(SCI_VISIBLEFROMDOCLINE, line)));
}

int DiffView::newFileLine(int line) const
{
    const int file = locate(line).file;
    if (file < 0)
        return 0;
    for (int i = line; i < lines_.size() && lines_[i].location.file == file; ++i) {
        if (lines_[i].newNumber > 0)
            return lines_[i].newNumber - 1;
    }
    // Removed lines at the end of the file: the line after the last one left.
    for (int i = line - 1; i >= 0 && lines_[i].location.file == file; --i) {
        if (lines_[i].newNumber > 0)
            return lines_[i].newNumber;
    }
    return 0;
}

int DiffView::lineTop(int line) const
{
    const long position = SendScintilla(SCI_POSITIONFROMLINE, line);
    return int(SendScintilla(SCI_POINTYFROMPOSITION, 0UL, position));
}

int DiffView::lineHeight() const
{
    return int(SendScintilla(SCI_TEXTHEIGHT, 0UL));
}

void DiffView::render()
{
    QStringList texts;
    texts.reserve(lines_.size());
    for (const DiffLine &line : std::as_const(lines_))
        texts.append(line.text);
    setReadOnly(false);
    setText(texts.join(QLatin1Char('\n')));
    setReadOnly(true);
    setModified(false);

    markerDeleteAll();
    clearMarginText();
    for (int i = 0; i < lines_.size(); ++i)
        styleLine(i, decorateLine(i));
    DiffSyntaxHighlighter highlighter(*this, theme_, lines_);
    for (int file = 0; file < fileStarts_.size(); ++file)
        highlighter.highlightFile(fileStarts_[file], files_.value(file).path);
    showNotes();

    // No numbers to show (a message): no margin.
    const bool numbered = std::any_of(lines_.begin(), lines_.end(),
                                      [](const DiffLine &line) { return line.oldNumber > 0 || line.newNumber > 0; });
    const QFontMetrics metrics(theme_.font);
    const int columns = numbers_ == NumberColumns::Both ? 2 : 1;
    const int digits = columns * numberWidth_ + 2 * columns + 1; // the spaces, the sign, a little air
    setMarginWidth(numberMargin, numbered ? metrics.horizontalAdvance(QString(digits, QLatin1Char('9'))) : 8);
}

QString DiffView::marginNumber(int value) const
{
    if (value <= 0)
        return {numberWidth_, QLatin1Char(' ')};
    return QString::number(value).rightJustified(numberWidth_);
}

int DiffView::decorateLine(int line)
{
    // Margin: "old new" numbers, and a sign the colors repeat.
    const DiffLine &shown = lines_[line];
    switch (shown.kind) {
    case DiffLineKind::Spacer:
        return plainStyle;
    case DiffLineKind::FileHeader:
        markerAdd(line, fileHeaderBackground);
        markerAdd(line, fileHeaderStrip);
        setMarginText(line, QStringLiteral(" "), fileMarginStyle);
        return fileHeaderStyle;
    case DiffLineKind::HunkHeader:
    case DiffLineKind::HiddenLines:
        markerAdd(line, hunkHeaderBackground);
        markerAdd(line, hunkHeaderStrip);
        setMarginText(line, QStringLiteral(" "), hunkMarginStyle);
        return hunkHeaderStyle;
    case DiffLineKind::Note:
        return noteStyle;
    case DiffLineKind::Filler:
        markerAdd(line, fillerBackground);
        return plainStyle;
    case DiffLineKind::Added:
        markerAdd(line, addedBackground);
        markerAdd(line, addedStrip);
        setMarginText(line, marginText(shown, QLatin1Char('+')), addedNumberStyle);
        return plainStyle;
    case DiffLineKind::Removed:
        markerAdd(line, removedBackground);
        markerAdd(line, removedStrip);
        setMarginText(line, marginText(shown, QChar(0x2212)), removedNumberStyle);
        return plainStyle;
    case DiffLineKind::Context:
        setMarginText(line, marginText(shown, QLatin1Char(' ')), numberStyle);
        return plainStyle;
    }
    return plainStyle;
}

QString DiffView::marginText(const DiffLine &line, QChar sign) const
{
    switch (numbers_) {
    case NumberColumns::Both:
        return QStringLiteral("%1 %2 %3").arg(marginNumber(line.oldNumber), marginNumber(line.newNumber), sign);
    case NumberColumns::OldOnly:
        return QStringLiteral("%1 %2").arg(marginNumber(line.oldNumber), sign);
    case NumberColumns::NewOnly:
        return QStringLiteral("%1 %2").arg(marginNumber(line.newNumber), sign);
    }
    return {};
}

void DiffView::styleLine(int line, int style)
{
    const DiffLine &shown = lines_[line];
    const long start = SendScintilla(SCI_POSITIONFROMLINE, line);
    const long length = SendScintilla(SCI_LINELENGTH, line);
    SendScintilla(SCI_STARTSTYLING, start);
    SendScintilla(SCI_SETSTYLING, length, style);
    if (shown.emphasis > 0) {
        SendScintilla(SCI_STARTSTYLING, start);
        SendScintilla(SCI_SETSTYLING, utf8Length(shown.text, shown.emphasis), fileNameStyle);
    }
    if (shown.statsStart >= 0)
        styleStats(shown, start, length);
    for (const RevealArrow &arrow : shown.arrows) {
        const long from = start + utf8Length(shown.text, arrow.position);
        SendScintilla(SCI_STARTSTYLING, from);
        SendScintilla(SCI_SETSTYLING, start + utf8Length(shown.text, arrow.position + 1) - from, revealStyle);
    }
    if (shown.changes.isEmpty())
        return;
    SendScintilla(SCI_SETINDICATORCURRENT, shown.kind == DiffLineKind::Added ? addedWords : removedWords);
    for (const TextRange &range : shown.changes) {
        const long from = start + utf8Length(shown.text, range.start);
        SendScintilla(SCI_INDICATORFILLRANGE, from, start + utf8Length(shown.text, range.start + range.length) - from);
    }
}

void DiffView::styleStats(const DiffLine &title, long start, long length)
{
    // "+12" in green, "−3" in red.
    const int minus = static_cast<int>(title.text.indexOf(QChar(0x2212), title.statsStart));
    const long statsStart = start + utf8Length(title.text, title.statsStart);
    const long minusStart = minus >= 0 ? start + utf8Length(title.text, minus) : start + length;
    SendScintilla(SCI_STARTSTYLING, statsStart);
    SendScintilla(SCI_SETSTYLING, minusStart - statsStart, addedStatsStyle);
    if (minus < 0)
        return;
    SendScintilla(SCI_STARTSTYLING, minusStart);
    SendScintilla(SCI_SETSTYLING, start + length - minusStart, removedStatsStyle);
}

void DiffView::applyTheme(const Theme &theme)
{
    theme_ = theme;
    setPaper(theme.background);
    setColor(theme.foreground);
    setFont(theme.font);
    setCaretForegroundColor(theme.caret);
    setSelectionBackgroundColor(theme.selection);
    SendScintilla(SCI_SETSELFORE, false, 0L);
    setMarginsBackgroundColor(theme.marginBackground);
    setMarginsForegroundColor(theme.marginForeground);
    setMarginsFont(theme.font);
    ScintillaStyles::applyTypography(*this, theme.font, theme.lineHeight);
    DiffStyles::define(*this, theme);
    // The old side first: rendering this side sets the old side's blank notes.
    if (oldSide_)
        oldSide_->applyTheme(theme);
    render(); // the styles of the text are set line by line
    // Side by side, the view's background shows between the two sides.
    QPalette rule = palette();
    rule.setColor(QPalette::Window, theme.line);
    setPalette(rule);
    setAutoFillBackground(true);
}

void DiffView::wheelEvent(QWheelEvent *event)
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
