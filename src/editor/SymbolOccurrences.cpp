#include "SymbolOccurrences.h"

#include "EditorWidget.h"
#include "ScrollBarMarks.h"

#include <QScrollBar>

namespace {

// Indicator of the occurrences (the first ones belong to lexers; the find
// bar, snippets, completion and diagnostics use 9 to 14).
constexpr int occurrenceIndicator = 15;
// The cursor must rest a moment: moving through the text with the arrows
// does not highlight every word on the way.
constexpr int settleDelayMs = 150;

bool isNameStart(char character)
{
    return std::isdigit(static_cast<unsigned char>(character)) == 0;
}

} // namespace

SymbolOccurrences::SymbolOccurrences(EditorWidget *editor, const Theme &theme)
    : QObject(editor)
    , editor_(editor)
    , marks_(new ScrollBarMarks(editor->verticalScrollBar()))
    , theme_(theme)
{
    settle_.setSingleShot(true);
    settle_.setInterval(settleDelayMs);
    connect(&settle_, &QTimer::timeout, this, &SymbolOccurrences::refreshNow);
    connect(editor_, &QsciScintilla::cursorPositionChanged, this, [this] {
        settledAt_ = -1;
        settle_.start();
    });
    connect(editor_, &QsciScintilla::selectionChanged, &settle_, qOverload<>(&QTimer::start));
    // The highlighted ranges no longer match the text.
    connect(editor_, &QsciScintilla::textChanged, this, [this] {
        settledAt_ = -1;
        clear();
        settle_.start();
    });
    styleIndicator();
}

void SymbolOccurrences::applyTheme(const Theme &theme)
{
    theme_ = theme;
    styleIndicator();
    show(occurrences_);
}

void SymbolOccurrences::styleIndicator()
{
    const bool dark = theme_.background.lightness() < 128;
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICSETSTYLE, occurrenceIndicator,
                           QsciScintillaBase::INDIC_ROUNDBOX);
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICSETFORE, occurrenceIndicator, theme_.selection);
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICSETALPHA, occurrenceIndicator, dark ? 110 : 90);
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICSETOUTLINEALPHA, occurrenceIndicator, 200);
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICSETUNDER, occurrenceIndicator, true);
}

void SymbolOccurrences::refreshNow()
{
    settle_.stop();
    const std::optional<Range> name = nameAtCursor();
    if (!name) {
        clear();
        return;
    }
    // Still on one of the shown occurrences: the server's answer stays.
    const bool onShownOccurrence = occurrences_.contains(*name);
    if (!onShownOccurrence)
        show(wholeWordMatches(editor_->utf8Range(name->start, name->end)));
    settledAt_ = name->start;
    emit nameSettled(name->start);
}

void SymbolOccurrences::showServerOccurrences(int position, const QList<Range> &ranges)
{
    if (position != settledAt_)
        return;
    show(ranges.mid(0, maximumOccurrences));
}

std::optional<SymbolOccurrences::Range> SymbolOccurrences::nameAtCursor() const
{
    const auto send = [this](unsigned int message, unsigned long wParam = 0, long lParam = 0) {
        return int(editor_->SendScintilla(message, wParam, lParam));
    };
    const int caret = send(QsciScintillaBase::SCI_GETCURRENTPOS);
    const int start = send(QsciScintillaBase::SCI_WORDSTARTPOSITION, caret, 1);
    const int end = send(QsciScintillaBase::SCI_WORDENDPOSITION, caret, 1);
    if (end <= start)
        return std::nullopt;
    // A selection counts only when it is that word (a double click).
    if (editor_->hasSelectedText()) {
        const int anchor = send(QsciScintillaBase::SCI_GETANCHOR);
        const bool selectsTheWord = std::min(anchor, caret) == start && std::max(anchor, caret) == end;
        if (!selectsTheWord)
            return std::nullopt;
    }
    if (!isNameStart(editor_->utf8Range(start, start + 1).at(0)))
        return std::nullopt; // a number
    return Range{start, end};
}

QList<SymbolOccurrences::Range> SymbolOccurrences::wholeWordMatches(const QByteArray &word) const
{
    const auto send = [this](unsigned int message, unsigned long wParam = 0, long lParam = 0) {
        return int(editor_->SendScintilla(message, wParam, lParam));
    };
    const int length = send(QsciScintillaBase::SCI_GETLENGTH);
    send(QsciScintillaBase::SCI_SETSEARCHFLAGS,
         QsciScintillaBase::SCFIND_WHOLEWORD | QsciScintillaBase::SCFIND_MATCHCASE);
    QList<Range> matches;
    int from = 0;
    while (matches.size() < maximumOccurrences) {
        send(QsciScintillaBase::SCI_SETTARGETRANGE, from, length);
        const int found =
            int(editor_->SendScintilla(QsciScintillaBase::SCI_SEARCHINTARGET, word.size(), word.constData()));
        if (found < 0)
            break;
        const int end = send(QsciScintillaBase::SCI_GETTARGETEND);
        matches.append({found, end});
        from = std::max(end, found + 1);
    }
    return matches;
}

void SymbolOccurrences::show(const QList<Range> &ranges)
{
    occurrences_ = ranges;
    const int length = int(editor_->SendScintilla(QsciScintillaBase::SCI_GETLENGTH));
    editor_->SendScintilla(QsciScintillaBase::SCI_SETINDICATORCURRENT, occurrenceIndicator);
    editor_->SendScintilla(QsciScintillaBase::SCI_INDICATORCLEARRANGE, 0, length);
    if (ranges.isEmpty()) {
        marks_->clear();
        return;
    }

    // The scroll bar stands for the displayed lines: folded ones take no
    // room.
    const int displayLines =
        std::max(int(editor_->SendScintilla(QsciScintillaBase::SCI_VISIBLEFROMDOCLINE, editor_->lines())), 1);
    QList<qreal> positions;
    for (const Range &range : ranges) {
        editor_->SendScintilla(QsciScintillaBase::SCI_INDICATORFILLRANGE, range.start, range.end - range.start);
        const int line = int(editor_->SendScintilla(QsciScintillaBase::SCI_LINEFROMPOSITION, range.start));
        const int displayLine = int(editor_->SendScintilla(QsciScintillaBase::SCI_VISIBLEFROMDOCLINE, line));
        positions.append(qreal(displayLine) / displayLines);
    }
    marks_->setMarks(positions, theme_.accent);
}

void SymbolOccurrences::clear()
{
    if (occurrences_.isEmpty())
        return;
    show({});
}
