#include "DiffView.h"

#include "syntax/HighlighterFactory.h"
#include "syntax/LanguageRegistry.h"
#include "syntax/ScintillaStyles.h"
#include "ui/AppFonts.h"

#include <QFontMetrics>

#include <algorithm>
#include <memory>
#include <vector>

namespace {

// Styles of the text (no lexer: they are set by hand) and of the margin;
// from firstSyntaxStyle, the colors of the code (see syntaxStyle()).
enum Style {
    PlainStyle = 0,
    FileHeaderStyle = 40,
    FileNameStyle,
    HunkHeaderStyle,
    NoteStyle,
    NumberStyle,
    AddedNumberStyle,
    RemovedNumberStyle,
    AddedStatsStyle,
    RemovedStatsStyle,
    FileMarginStyle,
    HunkMarginStyle
};

constexpr int firstSyntaxStyle = 64;
constexpr int lastSyntaxStyle = 255;

// Markers: full-line backgrounds, and a stronger strip in the side margin.
enum Marker {
    AddedBackground = 0,
    RemovedBackground,
    FileHeaderBackground,
    HunkHeaderBackground,
    AddedStrip,
    RemovedStrip,
    FileHeaderStrip,
    HunkHeaderStrip
};

// Indicators: the words that changed within a line.
enum Indicator {
    AddedWords = 8,
    RemovedWords
};

constexpr int numberMargin = 0;
constexpr int stripMargin = 1;
// Beyond this, a file's code is not colored: the diff appears at once.
constexpr qsizetype maxHighlightedBytes = 512 * 1024;

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
            mix(theme.background, red, theme.isDark ? 0.34 : 0.24),
            green,
            red,
            mix(theme.background, theme.foreground, theme.isDark ? 0.07 : 0.05),
            mix(theme.background, theme.accent, theme.isDark ? 0.09 : 0.06),
            theme.mutedText};
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
                        (1 << AddedStrip) | (1 << RemovedStrip) | (1 << FileHeaderStrip) | (1 << HunkHeaderStrip));
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
    const auto token = [](const QString &text, Range range) {
        return QStringView(text).mid(range.start, range.length);
    };

    // Longest common subsequence of the tokens, from the end.
    std::vector<int> common((n + 1) * (m + 1), 0);
    const auto at = [m](size_t i, size_t j) { return i * (m + 1) + j; };
    for (size_t i = n; i-- > 0;) {
        for (size_t j = m; j-- > 0;) {
            common[at(i, j)] = token(removed, before[int(i)]) == token(added, after[int(j)])
                                   ? common[at(i + 1, j + 1)] + 1
                                   : std::max(common[at(i + 1, j)], common[at(i, j + 1)]);
        }
    }
    std::vector<bool> keptBefore(n, false);
    std::vector<bool> keptAfter(m, false);
    int shared = 0;
    for (size_t i = 0, j = 0; i < n && j < m;) {
        if (token(removed, before[int(i)]) == token(added, after[int(j)])) {
            keptBefore[i] = keptAfter[j] = true;
            shared += visibleLength(token(removed, before[int(i)]));
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
        // "Name.cpp   src/folder · modified   +12 −3", the name brighter.
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
        title.location = {f, -1, -1};
        lines.append(title);

        const FilePatch &patch = file.patch;
        if (patch.hunks.isEmpty()) {
            const bool binary = std::any_of(patch.header.begin(), patch.header.end(), [](const QByteArray &line) {
                return line.startsWith("Binary files") || line.startsWith("GIT binary patch");
            });
            Line note;
            note.kind = LineKind::Note;
            note.text = binary ? tr("Binary file") : tr("No changes to the text");
            note.location = {f, -1, -1};
            lines.append(note);
            continue;
        }
        // Git takes the heading of a block from the last line starting with
        // a letter: in prose, a sentence cut short.
        const QString language = LanguageRegistry::languageForFile(file.path);
        const bool headings = language != LanguageRegistry::plainText && language != QLatin1String("markdown");
        for (int h = 0; h < patch.hunks.size(); ++h) {
            const DiffHunk &hunk = patch.hunks[h];
            Line separator;
            separator.kind = LineKind::HunkHeader;
            // Numbers of the new version, unless the block only removes.
            separator.text = QStringLiteral("⋯ ") + (hunk.newCount > 0 ? linesLabel(hunk.newStart, hunk.newCount)
                                                                       : linesLabel(hunk.oldStart, hunk.oldCount));
            if (const QString heading = QString::fromUtf8(hunk.heading).trimmed(); headings && !heading.isEmpty())
                separator.text += QStringLiteral("  ·  ") + heading;
            separator.location = {f, h, -1};
            lines.append(separator);

            const qsizetype first = lines.size();
            int oldNumber = hunk.oldStart;
            int newNumber = hunk.newStart;
            for (int l = 0; l < hunk.lines.size(); ++l) {
                const QByteArray &raw = hunk.lines[l];
                Line line;
                line.location = {f, h, l};
                QString text = QString::fromUtf8(raw.mid(1));
                if (text.endsWith(QLatin1Char('\r')))
                    text.chop(1);
                line.text = text;
                if (raw.startsWith('+')) {
                    line.kind = LineKind::Added;
                    line.newNumber = newNumber++;
                } else if (raw.startsWith('-')) {
                    line.kind = LineKind::Removed;
                    line.oldNumber = oldNumber++;
                } else if (raw.startsWith('\\')) {
                    line.kind = LineKind::Note;
                    line.text = tr("No line break at the end of the file");
                } else {
                    line.kind = LineKind::Context;
                    line.oldNumber = oldNumber++;
                    line.newNumber = newNumber++;
                }
                lines.append(line);
            }

            // Removed lines followed by added ones: each faces the one in
            // its place, and shows which words changed.
            for (qsizetype i = first; i < lines.size();) {
                qsizetype removedEnd = i;
                while (removedEnd < lines.size() && lines[removedEnd].kind == LineKind::Removed)
                    ++removedEnd;
                qsizetype addedEnd = removedEnd;
                while (addedEnd < lines.size() && lines[addedEnd].kind == LineKind::Added)
                    ++addedEnd;
                const qsizetype pairs = std::min(removedEnd - i, addedEnd - removedEnd);
                for (qsizetype k = 0; k < pairs; ++k) {
                    auto [before, after] = wordChanges(lines[i + k].text, lines[removedEnd + k].text);
                    lines[i + k].changes = std::move(before);
                    lines[removedEnd + k].changes = std::move(after);
                }
                i = std::max(addedEnd, i + 1);
            }
        }
    }
    return lines;
}

void DiffView::setFiles(const QList<DiffFile> &files)
{
    lines_ = layout(files);
    paths_.clear();
    for (const DiffFile &file : files)
        paths_.append(file.path);
    fileStarts_.clear();
    int largest = 0;
    for (int i = 0; i < lines_.size(); ++i) {
        if (lines_[i].kind == LineKind::FileHeader)
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
    paths_.clear();
    if (!message.isEmpty()) {
        Line note;
        note.kind = LineKind::Note;
        note.text = message;
        lines_.append(note);
    }
    render();
}

DiffView::Location DiffView::locate(int line) const
{
    return line >= 0 && line < lines_.size() ? lines_[line].location : Location{};
}

int DiffView::fileStart(int file) const
{
    return fileStarts_.value(file, 0);
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
    // Margin: "old new" numbers, and a sign the colors repeat.
    const QString blank(numberWidth_, QLatin1Char(' '));
    const auto number = [this, &blank](int value) {
        return value > 0 ? QString::number(value).rightJustified(numberWidth_) : blank;
    };
    const auto bytes = [](const QString &text, int characters) {
        return static_cast<long>(text.left(characters).toUtf8().size());
    };
    for (int i = 0; i < lines_.size(); ++i) {
        const Line &line = lines_[i];
        const long start = SendScintilla(SCI_POSITIONFROMLINE, i);
        const long length = SendScintilla(SCI_LINELENGTH, i);
        int style = PlainStyle;
        switch (line.kind) {
        case LineKind::Spacer:
            break;
        case LineKind::FileHeader:
            style = FileHeaderStyle;
            markerAdd(i, FileHeaderBackground);
            markerAdd(i, FileHeaderStrip);
            setMarginText(i, QStringLiteral(" "), FileMarginStyle);
            break;
        case LineKind::HunkHeader:
            style = HunkHeaderStyle;
            markerAdd(i, HunkHeaderBackground);
            markerAdd(i, HunkHeaderStrip);
            setMarginText(i, QStringLiteral(" "), HunkMarginStyle);
            break;
        case LineKind::Note:
            style = NoteStyle;
            break;
        case LineKind::Added:
            markerAdd(i, AddedBackground);
            markerAdd(i, AddedStrip);
            setMarginText(i, QStringLiteral("%1 %2 +").arg(blank, number(line.newNumber)), AddedNumberStyle);
            break;
        case LineKind::Removed:
            markerAdd(i, RemovedBackground);
            markerAdd(i, RemovedStrip);
            setMarginText(i, QStringLiteral("%1 %2 −").arg(number(line.oldNumber), blank), RemovedNumberStyle);
            break;
        case LineKind::Context:
            setMarginText(i, QStringLiteral("%1 %2  ").arg(number(line.oldNumber), number(line.newNumber)),
                          NumberStyle);
            break;
        }
        SendScintilla(SCI_STARTSTYLING, start);
        SendScintilla(SCI_SETSTYLING, length, style);
        if (line.emphasis > 0) {
            SendScintilla(SCI_STARTSTYLING, start);
            SendScintilla(SCI_SETSTYLING, bytes(line.text, line.emphasis), FileNameStyle);
        }
        if (line.statsStart >= 0) {
            // "+12" in green, "−3" in red.
            const int minus = static_cast<int>(line.text.indexOf(QChar(0x2212), line.statsStart));
            const long statsStart = start + bytes(line.text, line.statsStart);
            const long minusStart = minus >= 0 ? start + bytes(line.text, minus) : start + length;
            SendScintilla(SCI_STARTSTYLING, statsStart);
            SendScintilla(SCI_SETSTYLING, minusStart - statsStart, AddedStatsStyle);
            if (minus >= 0) {
                SendScintilla(SCI_STARTSTYLING, minusStart);
                SendScintilla(SCI_SETSTYLING, start + length - minusStart, RemovedStatsStyle);
            }
        }
        if (!line.changes.isEmpty()) {
            SendScintilla(SCI_SETINDICATORCURRENT, line.kind == LineKind::Added ? AddedWords : RemovedWords);
            for (const Range &range : line.changes) {
                const long from = start + bytes(line.text, range.start);
                SendScintilla(SCI_INDICATORFILLRANGE, from,
                              start + bytes(line.text, range.start + range.length) - from);
            }
        }
    }
    for (int file = 0; file < fileStarts_.size(); ++file)
        highlightSyntax(file);

    // No numbers to show (a message): no margin.
    const bool numbered = std::any_of(lines_.begin(), lines_.end(),
                                      [](const Line &line) { return line.oldNumber > 0 || line.newNumber > 0; });
    const QFontMetrics metrics(theme_.font);
    setMarginWidth(numberMargin,
                   numbered ? metrics.horizontalAdvance(QString(2 * numberWidth_ + 5, QLatin1Char('9'))) : 8);
}

void DiffView::highlightSyntax(int file)
{
    const QString language = LanguageRegistry::languageForFile(paths_.value(file));
    if (language == LanguageRegistry::plainText)
        return;

    // Both versions of the blocks, one after the other and a blank line
    // between them: removed lines are colored in the old one, the others in
    // the new one, each where it stands in its own code.
    QByteArray sides[2];

    struct Place {
        int line;
        int side;
        qsizetype offset;
    };

    QList<Place> places;
    int hunk = -1;
    for (int i = fileStarts_[file]; i < lines_.size() && lines_[i].location.file == file; ++i) {
        const Line &line = lines_[i];
        if (line.kind != LineKind::Context && line.kind != LineKind::Added && line.kind != LineKind::Removed)
            continue;
        if (line.location.hunk != hunk) {
            hunk = line.location.hunk;
            sides[0] += '\n';
            sides[1] += '\n';
        }
        const QByteArray text = line.text.toUtf8() + '\n';
        const int side = line.kind == LineKind::Removed ? 0 : 1;
        places.append({i, side, sides[side].size()});
        if (line.kind == LineKind::Context)
            sides[0] += text;
        sides[side] += text;
    }
    if (places.isEmpty() || sides[0].size() + sides[1].size() > maxHighlightedBytes)
        return;

    QsciScintilla scratch;
    const std::unique_ptr<ISyntaxHighlighter> highlighter = HighlighterFactory::create(language);
    if (!highlighter)
        return;
    highlighter->attach(&scratch);
    highlighter->applyTheme(theme_);

    // The scratch editor's styles become the diff's, by their look.
    const long plain = scintillaColor(theme_.foreground);
    std::vector<int> styleOf(256, -1);
    const auto diffStyle = [&](int scratchStyle) {
        int &style = styleOf[scratchStyle & 0xff];
        if (style < 0) {
            const long foreground = scratch.SendScintilla(SCI_STYLEGETFORE, scratchStyle);
            const bool bold = scratch.SendScintilla(SCI_STYLEGETBOLD, scratchStyle);
            const bool italic = scratch.SendScintilla(SCI_STYLEGETITALIC, scratchStyle);
            style = foreground == plain && !bold && !italic ? PlainStyle : syntaxStyle(foreground, bold, italic);
        }
        return style;
    };
    QByteArray styles[2];
    for (int side = 0; side < 2; ++side) {
        scratch.SendScintilla(SCI_SETTEXT, 0UL, sides[side].constData());
        scratch.SendScintilla(SCI_COLOURISE, 0UL, -1L);
        styles[side].resize(sides[side].size());
        for (qsizetype p = 0; p < sides[side].size(); ++p)
            styles[side][p] = static_cast<char>(scratch.SendScintilla(SCI_GETSTYLEAT, static_cast<unsigned long>(p)));
    }

    for (const Place &place : std::as_const(places)) {
        const long start = SendScintilla(SCI_POSITIONFROMLINE, place.line);
        const long length = static_cast<long>(lines_[place.line].text.toUtf8().size());
        const QByteArray &lineStyles = styles[place.side];
        for (long run = 0; run < length;) {
            const char scratchStyle = lineStyles[place.offset + run];
            long end = run + 1;
            while (end < length && lineStyles[place.offset + end] == scratchStyle)
                ++end;
            if (const int style = diffStyle(static_cast<unsigned char>(scratchStyle)); style != PlainStyle) {
                SendScintilla(SCI_STARTSTYLING, start + run);
                SendScintilla(SCI_SETSTYLING, end - run, style);
            }
            run = end;
        }
    }
}

int DiffView::syntaxStyle(long foreground, bool bold, bool italic)
{
    const qint64 key = (qint64(foreground) << 2) | (bold ? 2 : 0) | (italic ? 1 : 0);
    if (const auto it = syntaxStyles_.constFind(key); it != syntaxStyles_.constEnd())
        return it.value();
    const int style = firstSyntaxStyle + static_cast<int>(syntaxStyles_.size());
    if (style > lastSyntaxStyle)
        return PlainStyle;
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
    define(PlainStyle, theme_.foreground, theme_.background, theme_.font);
    define(FileHeaderStyle, colors.muted, colors.fileHeader, label);
    define(FileNameStyle, theme_.foreground, colors.fileHeader, name);
    define(HunkHeaderStyle, colors.muted, colors.hunkHeader, small);
    define(NoteStyle, colors.muted, theme_.background, italic);
    define(NumberStyle, theme_.marginForeground, theme_.marginBackground, theme_.font);
    define(AddedNumberStyle, colors.addedStrong, colors.added, theme_.font);
    define(RemovedNumberStyle, colors.removedStrong, colors.removed, theme_.font);
    define(AddedStatsStyle, colors.addedStrong, colors.fileHeader, stats);
    define(RemovedStatsStyle, colors.removedStrong, colors.fileHeader, stats);
    define(FileMarginStyle, colors.muted, colors.fileHeader, theme_.font);
    define(HunkMarginStyle, colors.muted, colors.hunkHeader, theme_.font);
    define(STYLE_LINENUMBER, theme_.marginForeground, theme_.marginBackground, theme_.font);

    const auto background = [this](int marker, const QColor &color) {
        markerDefine(QsciScintilla::Background, marker);
        setMarkerBackgroundColor(color, marker);
    };
    background(AddedBackground, colors.added);
    background(RemovedBackground, colors.removed);
    background(FileHeaderBackground, colors.fileHeader);
    background(HunkHeaderBackground, colors.hunkHeader);
    const auto strip = [this](int marker, const QColor &color) {
        markerDefine(QsciScintilla::FullRectangle, marker);
        setMarkerBackgroundColor(color, marker);
    };
    strip(AddedStrip, colors.addedStrong);
    strip(RemovedStrip, colors.removedStrong);
    strip(FileHeaderStrip, colors.fileHeader);
    strip(HunkHeaderStrip, colors.hunkHeader);

    const auto words = [this](int indicator, const QColor &color) {
        SendScintilla(SCI_INDICSETSTYLE, indicator, long(INDIC_FULLBOX));
        SendScintilla(SCI_INDICSETFORE, indicator, color);
        SendScintilla(SCI_INDICSETALPHA, indicator, 255L);
        SendScintilla(SCI_INDICSETOUTLINEALPHA, indicator, 0L);
        SendScintilla(SCI_INDICSETUNDER, indicator, 1L);
    };
    words(AddedWords, colors.addedWords);
    words(RemovedWords, colors.removedWords);
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
