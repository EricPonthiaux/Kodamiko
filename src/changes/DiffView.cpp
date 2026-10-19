#include "DiffView.h"

#include "syntax/HighlighterFactory.h"
#include "syntax/LanguageRegistry.h"
#include "syntax/ScintillaStyles.h"
#include "ui/AppFonts.h"

#include <QFontMetrics>

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <vector>

namespace {

// Styles of the text (no lexer: they are set by hand) and of the margin;
// from firstSyntaxStyle, the colors of the code (see syntaxStyle()).
constexpr int plainStyle = 0;
constexpr int fileHeaderStyle = 40;
constexpr int fileNameStyle = 41;
constexpr int hunkHeaderStyle = 42;
constexpr int noteStyle = 43;
constexpr int numberStyle = 44;
constexpr int addedNumberStyle = 45;
constexpr int removedNumberStyle = 46;
constexpr int addedStatsStyle = 47;
constexpr int removedStatsStyle = 48;
constexpr int fileMarginStyle = 49;
constexpr int hunkMarginStyle = 50;

constexpr int firstSyntaxStyle = 64;
constexpr int lastSyntaxStyle = 255;

// Markers: full-line backgrounds, and a stronger strip in the side margin.
constexpr int addedBackground = 0;
constexpr int removedBackground = 1;
constexpr int fileHeaderBackground = 2;
constexpr int hunkHeaderBackground = 3;
constexpr int addedStrip = 4;
constexpr int removedStrip = 5;
constexpr int fileHeaderStrip = 6;
constexpr int hunkHeaderStrip = 7;

// Indicators: the words that changed within a line.
constexpr int addedWords = 8;
constexpr int removedWords = 9;

constexpr int numberMargin = 0;
constexpr int stripMargin = 1;
// Beyond these, a file's code, then the remaining files' code, is not
// colored: the diff appears at once.
constexpr qsizetype maxHighlightedFileBytes = qsizetype(512) * 1024;
constexpr qsizetype maxHighlightedBytes = qsizetype(2) * 1024 * 1024;

QColor mix(const QColor &a, const QColor &b, double share)
{
    return QColor::fromRgbF(a.redF() * (1 - share) + b.redF() * share, a.greenF() * (1 - share) + b.greenF() * share,
                            a.blueF() * (1 - share) + b.blueF() * share);
}

struct DiffColors {
    QColor added;
    QColor removed;
    QColor addedWords;
    QColor removedWords;
    QColor addedStrong;
    QColor removedStrong;
    QColor fileHeader;
    QColor hunkHeader;
    QColor muted;
};

DiffColors colorsFor(const Theme &theme)
{
    const QColor green = theme.isDark ? QColor("#3fb950") : QColor("#1a7f37");
    const QColor red = theme.isDark ? QColor("#f85149") : QColor("#cf222e");
    return {mix(theme.background, green, theme.isDark ? 0.13 : 0.10),
            mix(theme.background, red, theme.isDark ? 0.13 : 0.09),
            mix(theme.background, green, theme.isDark ? 0.34 : 0.28),
            mix(theme.background, red, theme.isDark ? 0.34 : 0.24), green, red,
            mix(theme.background, theme.foreground, theme.isDark ? 0.07 : 0.05),
            // The block separators: neutral, fainter than the code.
            mix(theme.background, theme.foreground, theme.isDark ? 0.035 : 0.03), theme.mutedText};
}

// Scintilla's colors: red in the low byte.
long scintillaColor(const QColor &color)
{
    return color.red() | (color.green() << 8) | (color.blue() << 16);
}

QString linesLabel(int start, int count)
{
    if (count <= 1)
        return DiffView::tr("line %1").arg(start);
    return DiffView::tr("lines %1–%2").arg(start).arg(start + count - 1);
}

// Words, runs of spaces, and every other character on its own.
QList<DiffView::Range> tokenize(const QString &text)
{
    const auto isWord = [](QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); };
    QList<DiffView::Range> tokens;
    for (int i = 0; i < text.size();) {
        int end = i + 1;
        if (isWord(text[i])) {
            while (end < text.size() && isWord(text[end]))
                ++end;
        } else if (text[i].isSpace()) {
            while (end < text.size() && text[end].isSpace())
                ++end;
        }
        tokens.append({i, end - i});
        i = end;
    }
    return tokens;
}

int visibleLength(QStringView token)
{
    return static_cast<int>(std::count_if(token.begin(), token.end(), [](QChar c) { return !c.isSpace(); }));
}

// The tokens not kept, joined into ranges; a space between two of them
// goes with them.
QList<DiffView::Range> changedRanges(const QString &text, const QList<DiffView::Range> &tokens, std::vector<bool> kept)
{
    for (size_t i = 1; i + 1 < kept.size(); ++i) {
        const DiffView::Range token = tokens[static_cast<int>(i)];
        if (kept[i] && !kept[i - 1] && !kept[i + 1] && text[token.start].isSpace())
            kept[i] = false;
    }
    QList<DiffView::Range> ranges;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (kept[i])
            continue;
        const DiffView::Range token = tokens[static_cast<int>(i)];
        if (!ranges.isEmpty() && ranges.last().start + ranges.last().length == token.start)
            ranges.last().length += token.length;
        else
            ranges.append(token);
    }
    return ranges;
}

using Line = DiffView::Line;
using LineKind = DiffView::LineKind;
using Range = DiffView::Range;

QStringView tokenText(const QString &text, Range token)
{
    return QStringView(text).mid(token.start, token.length);
}

// Longest common subsequence of the tokens of `removed` and `added`: the
// length of the one from each pair of positions (i, j), in a table of
// (before.size() + 1) × (after.size() + 1), filled from the end.
std::vector<int> commonLengths(const QString &removed,
                               const QList<Range> &before,
                               const QString &added,
                               const QList<Range> &after)
{
    const size_t n = before.size();
    const size_t m = after.size();
    std::vector<int> common((n + 1) * (m + 1), 0);
    const auto at = [m](size_t i, size_t j) { return i * (m + 1) + j; };
    for (size_t i = n; i-- > 0;) {
        for (size_t j = m; j-- > 0;) {
            const bool isSame = tokenText(removed, before[int(i)]) == tokenText(added, after[int(j)]);
            common[at(i, j)] =
                isSame ? common[at(i + 1, j + 1)] + 1 : std::max(common[at(i + 1, j)], common[at(i, j + 1)]);
        }
    }
    return common;
}

// "Name.cpp   src/folder · modified   +12 −3", the name brighter.
Line fileTitle(const DiffFile &file, int fileIndex)
{
    const qsizetype slash = file.path.lastIndexOf(QLatin1Char('/'));
    const QString name = file.path.mid(slash + 1);
    QStringList details;
    if (slash >= 0)
        details.append(file.path.left(slash));
    if (!file.status.isEmpty())
        details.append(file.status);
    Line title;
    title.kind = LineKind::FileHeader;
    title.text = details.isEmpty() ? name : name + QStringLiteral("   ") + details.join(QStringLiteral("  ·  "));
    title.emphasis = static_cast<int>(name.size());
    QStringList stats;
    if (const int added = file.patch.addedLines(); added > 0)
        stats.append(QStringLiteral("+%1").arg(added));
    if (const int removed = file.patch.removedLines(); removed > 0)
        stats.append(QStringLiteral("−%1").arg(removed));
    if (!stats.isEmpty()) {
        title.text += QStringLiteral("   ");
        title.statsStart = static_cast<int>(title.text.size());
        title.text += stats.join(QLatin1Char(' '));
    }
    title.location = {fileIndex, -1, -1};
    return title;
}

QString noTextReason(const DiffFile &file)
{
    if (file.isTooLarge)
        return DiffView::tr("File too large to show");
    const QList<QByteArray> &header = file.patch.header;
    const bool binary = std::any_of(header.begin(), header.end(), [](const QByteArray &line) {
        return line.startsWith("Binary files") || line.startsWith("GIT binary patch");
    });
    return binary ? DiffView::tr("Binary file") : DiffView::tr("No changes to the text");
}

// What stands in place of the blocks of a patch that has none.
Line noTextNote(const DiffFile &file, int fileIndex)
{
    Line note;
    note.kind = LineKind::Note;
    note.text = noTextReason(file);
    note.location = {fileIndex, -1, -1};
    return note;
}

// "⋯ lines 40–48 · function": the numbers of the new version, unless the
// block only removes.
Line hunkSeparator(const DiffHunk &hunk, DiffView::Location location, bool withHeading)
{
    Line separator;
    separator.kind = LineKind::HunkHeader;
    const QString lines =
        hunk.newCount > 0 ? linesLabel(hunk.newStart, hunk.newCount) : linesLabel(hunk.oldStart, hunk.oldCount);
    separator.text = QStringLiteral("⋯ ") + lines;
    if (const QString heading = QString::fromUtf8(hunk.heading).trimmed(); withHeading && !heading.isEmpty())
        separator.text += QStringLiteral("  ·  ") + heading;
    separator.location = location;
    return separator;
}

// The next line numbers on each side of a block.
struct LineNumbers {
    int oldNumber = 0;
    int newNumber = 0;
};

// A line of a block, without git's "+", "-" or " "; advances `numbers`
// past it.
Line patchLine(const QByteArray &raw, DiffView::Location location, LineNumbers &numbers)
{
    Line line;
    line.location = location;
    line.text = QString::fromUtf8(raw.mid(1));
    if (line.text.endsWith(QLatin1Char('\r')))
        line.text.chop(1);
    if (raw.startsWith('+')) {
        line.kind = LineKind::Added;
        line.newNumber = numbers.newNumber++;
    } else if (raw.startsWith('-')) {
        line.kind = LineKind::Removed;
        line.oldNumber = numbers.oldNumber++;
    } else if (raw.startsWith('\\')) {
        line.kind = LineKind::Note;
        line.text = DiffView::tr("No line break at the end of the file");
    } else {
        line.kind = LineKind::Context;
        line.oldNumber = numbers.oldNumber++;
        line.newNumber = numbers.newNumber++;
    }
    return line;
}

qsizetype endOfRun(const QList<Line> &lines, qsizetype from, LineKind kind)
{
    qsizetype end = from;
    while (end < lines.size() && lines[end].kind == kind)
        ++end;
    return end;
}

// Removed lines followed by added ones, from `first`: each faces the one in
// its place, and shows which words changed.
void markChangedWords(QList<Line> &lines, qsizetype first)
{
    for (qsizetype i = first; i < lines.size();) {
        const qsizetype removedEnd = endOfRun(lines, i, LineKind::Removed);
        const qsizetype addedEnd = endOfRun(lines, removedEnd, LineKind::Added);
        const qsizetype pairs = std::min(removedEnd - i, addedEnd - removedEnd);
        for (qsizetype k = 0; k < pairs; ++k) {
            auto [before, after] = DiffView::wordChanges(lines[i + k].text, lines[removedEnd + k].text);
            lines[i + k].changes = std::move(before);
            lines[removedEnd + k].changes = std::move(after);
        }
        i = std::max(addedEnd, i + 1);
    }
}

void appendHunk(QList<Line> &lines, const DiffHunk &hunk, DiffView::Location location, bool withHeading)
{
    lines.append(hunkSeparator(hunk, location, withHeading));
    const qsizetype first = lines.size();
    LineNumbers numbers{hunk.oldStart, hunk.newStart};
    for (int l = 0; l < hunk.lines.size(); ++l)
        lines.append(patchLine(hunk.lines[l], {location.file, location.hunk, l}, numbers));
    markChangedWords(lines, first);
}

// Bytes of the first `characters` of `text`, as Scintilla counts.
long utf8Length(const QString &text, int characters)
{
    return static_cast<long>(text.left(characters).toUtf8().size());
}

// Both versions of a file's blocks, one after the other and a blank line
// between them: removed lines are colored in the old one, the others in
// the new one, each where it stands in its own code.
constexpr int oldSide = 0;
constexpr int newSide = 1;

struct SidePlace {
    int line;
    int side;
    qsizetype offset; // in the side's code
};

struct BothSides {
    std::array<QByteArray, 2> code;
    QList<SidePlace> places;
};

bool isCodeLine(const Line &line)
{
    return line.kind == LineKind::Context || line.kind == LineKind::Added || line.kind == LineKind::Removed;
}

BothSides collectSides(const QList<Line> &lines, int fileStart, int file)
{
    BothSides sides;
    int hunk = -1;
    for (int i = fileStart; i < lines.size() && lines[i].location.file == file; ++i) {
        const Line &line = lines[i];
        if (!isCodeLine(line))
            continue;
        if (line.location.hunk != hunk) {
            hunk = line.location.hunk;
            sides.code[oldSide] += '\n';
            sides.code[newSide] += '\n';
        }
        const QByteArray text = line.text.toUtf8() + '\n';
        const int side = line.kind == LineKind::Removed ? oldSide : newSide;
        sides.places.append({i, side, sides.code[side].size()});
        if (line.kind == LineKind::Context)
            sides.code[oldSide] += text;
        sides.code[side] += text;
    }
    return sides;
}

// The style of each byte of each side, as `scratch` colors it. One
// SCI_GETSTYLEDTEXT per side rather than a message per byte: up to
// maxHighlightedFileBytes of them.
std::array<QByteArray, 2> scratchStyles(const QsciScintilla &scratch, const std::array<QByteArray, 2> &code)
{
    std::array<QByteArray, 2> styles;
    for (size_t side = 0; side < code.size(); ++side) {
        const qsizetype length = code[side].size();
        scratch.SendScintilla(QsciScintillaBase::SCI_SETTEXT, 0UL, code[side].constData());
        scratch.SendScintilla(QsciScintillaBase::SCI_COLOURISE, 0UL, -1L);
        // Each byte followed by its style, then two zero bytes.
        QByteArray styled(2 * length + 2, '\0');
        scratch.SendScintilla(QsciScintillaBase::SCI_GETSTYLEDTEXT, 0L, long(length), styled.data());
        styles[side].resize(length);
        for (qsizetype p = 0; p < length; ++p)
            styles[side][p] = styled[2 * p + 1];
    }
    return styles;
}

} // namespace

// Off-screen editors that color code, one per language, for one render:
// creating an editor and its highlighter for each file would cost more
// than coloring the file.
class ScratchEditors {
public:
    explicit ScratchEditors(const Theme &theme)
        : theme_(theme)
    {
    }

    // The editor coloring `language`; nullptr if the language has no highlighter.
    QsciScintilla *forLanguage(const QString &language)
    {
        if (const auto it = scratches_.find(language); it != scratches_.end())
            return it->second.editor.get();
        Scratch scratch{std::make_unique<QsciScintilla>(), HighlighterFactory::create(language)};
        if (scratch.highlighter) {
            scratch.highlighter->attach(scratch.editor.get());
            scratch.highlighter->applyTheme(theme_);
        } else {
            scratch.editor.reset();
        }
        return scratches_.emplace(language, std::move(scratch)).first->second.editor.get();
    }

private:
    // The highlighter goes before the editor it is attached to.
    struct Scratch {
        std::unique_ptr<QsciScintilla> editor;
        std::unique_ptr<ISyntaxHighlighter> highlighter;
    };

    const Theme &theme_;
    std::map<QString, Scratch> scratches_;
};

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
    applyTheme(theme);
}

std::pair<QList<DiffView::Range>, QList<DiffView::Range>> DiffView::wordChanges(const QString &removed,
                                                                                const QString &added)
{
    const QList<Range> before = tokenize(removed);
    const QList<Range> after = tokenize(added);
    const size_t n = before.size();
    const size_t m = after.size();
    if (n == 0 || m == 0 || n * m > 40000)
        return {};

    const std::vector<int> common = commonLengths(removed, before, added, after);
    const auto at = [m](size_t i, size_t j) { return i * (m + 1) + j; };
    std::vector<bool> keptBefore(n, false);
    std::vector<bool> keptAfter(m, false);
    int shared = 0;
    for (size_t i = 0, j = 0; i < n && j < m;) {
        if (tokenText(removed, before[int(i)]) == tokenText(added, after[int(j)])) {
            keptBefore[i] = keptAfter[j] = true;
            shared += visibleLength(tokenText(removed, before[int(i)]));
            ++i;
            ++j;
        } else if (common[at(i + 1, j)] >= common[at(i, j + 1)]) {
            ++i;
        } else {
            ++j;
        }
    }
    // Mostly rewritten: brighter words everywhere would only add noise.
    const int total = visibleLength(removed) + visibleLength(added);
    if (total == 0 || 2 * shared < total / 2)
        return {};
    return {changedRanges(removed, before, keptBefore), changedRanges(added, after, keptAfter)};
}

QList<DiffView::Line> DiffView::layout(const QList<DiffFile> &files)
{
    QList<Line> lines;
    for (int f = 0; f < files.size(); ++f) {
        const DiffFile &file = files[f];
        // A blank line before each file: they breathe.
        Line spacer;
        spacer.kind = LineKind::Spacer;
        lines.append(spacer);
        lines.append(fileTitle(file, f));

        const FilePatch &patch = file.patch;
        if (patch.hunks.isEmpty()) {
            lines.append(noTextNote(file, f));
            continue;
        }
        // Git takes the heading of a block from the last line starting with
        // a letter: in prose, a sentence cut short.
        const QString language = LanguageRegistry::languageForFile(file.path);
        const bool headings = language != LanguageRegistry::plainText && language != QLatin1String("markdown");
        for (int h = 0; h < patch.hunks.size(); ++h)
            appendHunk(lines, patch.hunks[h], {f, h, -1}, headings);
    }
    return lines;
}

void DiffView::setFiles(const QList<DiffFile> &files)
{
    lines_ = layout(files);
    paths_.clear();
    summaries_.clear();
    for (const DiffFile &file : files) {
        paths_.append(file.path);
        summaries_.append({file.path, file.status, file.patch.addedLines(), file.patch.removedLines()});
    }
    fileStarts_.clear();
    int largest = 0;
    for (int i = 0; i < lines_.size(); ++i) {
        if (lines_[i].kind == LineKind::FileHeader)
            fileStarts_.append(i);
        largest = std::max({largest, lines_[i].oldNumber, lines_[i].newNumber});
    }
    numberWidth_ = std::max(3, static_cast<int>(QString::number(largest).size()));
    render();
    emit contentChanged();
}

void DiffView::showMessage(const QString &message)
{
    lines_.clear();
    fileStarts_.clear();
    paths_.clear();
    summaries_.clear();
    if (!message.isEmpty()) {
        Line note;
        note.kind = LineKind::Note;
        note.text = message;
        lines_.append(note);
    }
    render();
    emit contentChanged();
}

DiffView::Location DiffView::locate(int line) const
{
    return line >= 0 && line < lines_.size() ? lines_[line].location : Location{};
}

int DiffView::fileStart(int file) const
{
    return fileStarts_.value(file, 0);
}

int DiffView::hunkStart(int line) const
{
    const Location location = locate(line);
    if (location.hunk < 0)
        return -1;
    for (int i = line; i >= 0; --i) {
        if (lines_[i].kind == LineKind::HunkHeader)
            return i;
    }
    return -1;
}

void DiffView::goToChange(bool forward)
{
    const int first = firstVisibleLine();
    const int step = forward ? 1 : -1;
    for (int i = first + step; i >= 0 && i < lines_.size(); i += step) {
        if (lines_[i].kind == LineKind::HunkHeader) {
            setFirstVisibleLine(i);
            return;
        }
    }
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
    for (const Line &line : std::as_const(lines_))
        texts.append(line.text);
    setReadOnly(false);
    setText(texts.join(QLatin1Char('\n')));
    setReadOnly(true);
    setModified(false);

    markerDeleteAll();
    clearMarginText();
    for (int i = 0; i < lines_.size(); ++i)
        styleLine(i, decorateLine(i));
    highlightFiles();

    // No numbers to show (a message): no margin.
    const bool numbered = std::any_of(lines_.begin(), lines_.end(),
                                      [](const Line &line) { return line.oldNumber > 0 || line.newNumber > 0; });
    const QFontMetrics metrics(theme_.font);
    setMarginWidth(numberMargin,
                   numbered ? metrics.horizontalAdvance(QString(2 * numberWidth_ + 5, QLatin1Char('9'))) : 8);
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
    const Line &shown = lines_[line];
    const QString blank = marginNumber(0);
    switch (shown.kind) {
    case LineKind::Spacer:
        return plainStyle;
    case LineKind::FileHeader:
        markerAdd(line, fileHeaderBackground);
        markerAdd(line, fileHeaderStrip);
        setMarginText(line, QStringLiteral(" "), fileMarginStyle);
        return fileHeaderStyle;
    case LineKind::HunkHeader:
        markerAdd(line, hunkHeaderBackground);
        markerAdd(line, hunkHeaderStrip);
        setMarginText(line, QStringLiteral(" "), hunkMarginStyle);
        return hunkHeaderStyle;
    case LineKind::Note:
        return noteStyle;
    case LineKind::Added:
        markerAdd(line, addedBackground);
        markerAdd(line, addedStrip);
        setMarginText(line, QStringLiteral("%1 %2 +").arg(blank, marginNumber(shown.newNumber)), addedNumberStyle);
        return plainStyle;
    case LineKind::Removed:
        markerAdd(line, removedBackground);
        markerAdd(line, removedStrip);
        setMarginText(line, QStringLiteral("%1 %2 −").arg(marginNumber(shown.oldNumber), blank), removedNumberStyle);
        return plainStyle;
    case LineKind::Context:
        setMarginText(line, QStringLiteral("%1 %2  ").arg(marginNumber(shown.oldNumber), marginNumber(shown.newNumber)),
                      numberStyle);
        return plainStyle;
    }
    return plainStyle;
}

void DiffView::styleLine(int line, int style)
{
    const Line &shown = lines_[line];
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
    if (shown.changes.isEmpty())
        return;
    SendScintilla(SCI_SETINDICATORCURRENT, shown.kind == LineKind::Added ? addedWords : removedWords);
    for (const Range &range : shown.changes) {
        const long from = start + utf8Length(shown.text, range.start);
        SendScintilla(SCI_INDICATORFILLRANGE, from, start + utf8Length(shown.text, range.start + range.length) - from);
    }
}

void DiffView::styleStats(const Line &title, long start, long length)
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

void DiffView::highlightFiles()
{
    ScratchEditors scratches(theme_);
    qsizetype budget = maxHighlightedBytes;
    for (int file = 0; file < fileStarts_.size(); ++file)
        budget -= highlightSyntax(file, scratches, budget);
}

qsizetype DiffView::highlightSyntax(int file, ScratchEditors &scratches, qsizetype budget)
{
    const QString language = LanguageRegistry::languageForFile(paths_.value(file));
    if (language == LanguageRegistry::plainText)
        return 0;

    const BothSides sides = collectSides(lines_, fileStarts_[file], file);
    const qsizetype size = sides.code[oldSide].size() + sides.code[newSide].size();
    if (sides.places.isEmpty() || size > std::min(budget, maxHighlightedFileBytes))
        return 0;
    QsciScintilla *scratch = scratches.forLanguage(language);
    if (!scratch)
        return 0;
    const std::array<QByteArray, 2> styles = scratchStyles(*scratch, sides.code);

    // The scratch editor's styles become the diff's, by their look.
    const long plain = scintillaColor(theme_.foreground);
    std::vector<int> styleOf(256, -1);
    const auto diffStyle = [&](int scratchStyle) {
        int &style = styleOf[scratchStyle & 0xff];
        if (style >= 0)
            return style;
        const long foreground = scratch->SendScintilla(SCI_STYLEGETFORE, scratchStyle);
        const bool bold = scratch->SendScintilla(SCI_STYLEGETBOLD, scratchStyle);
        const bool italic = scratch->SendScintilla(SCI_STYLEGETITALIC, scratchStyle);
        const bool isPlain = foreground == plain && !bold && !italic;
        style = isPlain ? plainStyle : syntaxStyle(foreground, bold, italic);
        return style;
    };
    for (const SidePlace &place : sides.places)
        styleRuns(place.line, QByteArrayView(styles[place.side]).sliced(place.offset), diffStyle);
    return size;
}

void DiffView::styleRuns(int line, QByteArrayView scratchStyles, const std::function<int(int)> &diffStyle)
{
    const long start = SendScintilla(SCI_POSITIONFROMLINE, line);
    const long length = static_cast<long>(lines_[line].text.toUtf8().size());
    for (long run = 0; run < length;) {
        const char scratchStyle = scratchStyles[run];
        long end = run + 1;
        while (end < length && scratchStyles[end] == scratchStyle)
            ++end;
        if (const int style = diffStyle(static_cast<unsigned char>(scratchStyle)); style != plainStyle) {
            SendScintilla(SCI_STARTSTYLING, start + run);
            SendScintilla(SCI_SETSTYLING, end - run, style);
        }
        run = end;
    }
}

int DiffView::syntaxStyle(long foreground, bool bold, bool italic)
{
    const qint64 key = (qint64(foreground) << 2) | (bold ? 2 : 0) | (italic ? 1 : 0);
    if (const auto it = syntaxStyles_.constFind(key); it != syntaxStyles_.constEnd())
        return it.value();
    const int style = firstSyntaxStyle + static_cast<int>(syntaxStyles_.size());
    if (style > lastSyntaxStyle)
        return plainStyle;
    const QByteArray family = theme_.font.family().toUtf8();
    SendScintilla(SCI_STYLESETFORE, style, foreground);
    SendScintilla(SCI_STYLESETBACK, style, theme_.background);
    SendScintilla(SCI_STYLESETFONT, style, family.constData());
    SendScintilla(SCI_STYLESETSIZEFRACTIONAL, style, qRound(theme_.font.pointSizeF() * int(SC_FONT_SIZE_MULTIPLIER)));
    SendScintilla(SCI_STYLESETWEIGHT, style, ScintillaStyles::styleWeight(theme_.font, bold));
    SendScintilla(SCI_STYLESETITALIC, style, italic);
    syntaxStyles_.insert(key, style);
    return style;
}

void DiffView::defineStyles()
{
    const DiffColors colors = colorsFor(theme_);
    // Title bars and separators in the interface's font, the code in the editor's.
    QFont label = AppFonts::interfaceFont();
    label.setPointSizeF(theme_.font.pointSizeF() * 0.95);
    QFont small = label;
    small.setPointSizeF(label.pointSizeF() * 0.92);
    QFont name = label;
    name.setBold(true);
    QFont stats = theme_.font;
    stats.setBold(true);
    QFont italic = label;
    italic.setItalic(true);
    const auto define = [this](int style, const QColor &foreground, const QColor &background, const QFont &font) {
        SendScintilla(SCI_STYLESETFORE, style, foreground);
        SendScintilla(SCI_STYLESETBACK, style, background);
        SendScintilla(SCI_STYLESETFONT, style, font.family().toUtf8().constData());
        SendScintilla(SCI_STYLESETSIZEFRACTIONAL, style, qRound(font.pointSizeF() * int(SC_FONT_SIZE_MULTIPLIER)));
        SendScintilla(SCI_STYLESETWEIGHT, style, font.weight());
        SendScintilla(SCI_STYLESETITALIC, style, font.italic());
    };
    // The default style first: STYLECLEARALL copies it to every style.
    define(STYLE_DEFAULT, theme_.foreground, theme_.background, theme_.font);
    SendScintilla(SCI_STYLECLEARALL);
    syntaxStyles_.clear();
    define(plainStyle, theme_.foreground, theme_.background, theme_.font);
    define(fileHeaderStyle, colors.muted, colors.fileHeader, label);
    define(fileNameStyle, theme_.foreground, colors.fileHeader, name);
    define(hunkHeaderStyle, colors.muted, colors.hunkHeader, small);
    define(noteStyle, colors.muted, theme_.background, italic);
    define(numberStyle, theme_.marginForeground, theme_.marginBackground, theme_.font);
    define(addedNumberStyle, colors.addedStrong, colors.added, theme_.font);
    define(removedNumberStyle, colors.removedStrong, colors.removed, theme_.font);
    define(addedStatsStyle, colors.addedStrong, colors.fileHeader, stats);
    define(removedStatsStyle, colors.removedStrong, colors.fileHeader, stats);
    define(fileMarginStyle, colors.muted, colors.fileHeader, theme_.font);
    define(hunkMarginStyle, colors.muted, colors.hunkHeader, theme_.font);
    define(STYLE_LINENUMBER, theme_.marginForeground, theme_.marginBackground, theme_.font);

    const auto background = [this](int marker, const QColor &color) {
        markerDefine(QsciScintilla::Background, marker);
        setMarkerBackgroundColor(color, marker);
    };
    background(addedBackground, colors.added);
    background(removedBackground, colors.removed);
    background(fileHeaderBackground, colors.fileHeader);
    background(hunkHeaderBackground, colors.hunkHeader);
    const auto strip = [this](int marker, const QColor &color) {
        markerDefine(QsciScintilla::FullRectangle, marker);
        setMarkerBackgroundColor(color, marker);
    };
    strip(addedStrip, colors.addedStrong);
    strip(removedStrip, colors.removedStrong);
    strip(fileHeaderStrip, colors.fileHeader);
    strip(hunkHeaderStrip, colors.hunkHeader);

    const auto words = [this](int indicator, const QColor &color) {
        SendScintilla(SCI_INDICSETSTYLE, indicator, long(INDIC_FULLBOX));
        SendScintilla(SCI_INDICSETFORE, indicator, color);
        SendScintilla(SCI_INDICSETALPHA, indicator, 255L);
        SendScintilla(SCI_INDICSETOUTLINEALPHA, indicator, 0L);
        SendScintilla(SCI_INDICSETUNDER, indicator, 1L);
    };
    words(addedWords, colors.addedWords);
    words(removedWords, colors.removedWords);
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
    defineStyles();
    render(); // the styles of the text are set line by line
}
