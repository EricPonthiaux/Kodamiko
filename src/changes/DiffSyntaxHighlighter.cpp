// SPDX-License-Identifier: GPL-3.0-only

#include "DiffSyntaxHighlighter.h"

#include "DiffStyles.h"
#include "syntax/LanguageRegistry.h"
#include "syntax/ScintillaStyles.h"

#include <algorithm>
#include <array>
#include <vector>

using namespace DiffStyles;

namespace {

constexpr qsizetype maxHighlightedFileBytes = qsizetype(512) * 1024;
constexpr qsizetype maxHighlightedBytes = qsizetype(2) * 1024 * 1024;

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

long scintillaColor(const QColor &color)
{
    return color.red() | (color.green() << 8) | (color.blue() << 16);
}

bool isCodeLine(const DiffLine &line)
{
    return line.kind == DiffLineKind::Context || line.kind == DiffLineKind::Added || line.kind == DiffLineKind::Removed;
}

bool isSeparator(const DiffLine &line)
{
    return line.kind == DiffLineKind::FileHeader || line.kind == DiffLineKind::HunkHeader ||
           line.kind == DiffLineKind::HiddenLines;
}

// The code of both sides, a blank line where lines are left out.
BothSides collectSides(const QList<DiffLine> &lines, int fileStart, int file)
{
    BothSides sides;
    for (int i = fileStart + 1; i < lines.size() && lines[i].location.file == file; ++i) {
        const DiffLine &line = lines[i];
        if (!isCodeLine(line))
            continue;
        if (isSeparator(lines[i - 1])) {
            sides.code[oldSide] += '\n';
            sides.code[newSide] += '\n';
        }
        const QByteArray text = line.text.toUtf8() + '\n';
        const int side = line.kind == DiffLineKind::Removed ? oldSide : newSide;
        sides.places.append({i, side, sides.code[side].size()});
        if (line.kind == DiffLineKind::Context)
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

DiffSyntaxHighlighter::DiffSyntaxHighlighter(QsciScintilla &view, const Theme &theme, const QList<DiffLine> &lines)
    : view_(view)
    , theme_(theme)
    , lines_(lines)
    , scratches_(theme)
    , budget_(maxHighlightedBytes)
{
}

void DiffSyntaxHighlighter::highlightFile(int fileStart, const QString &path)
{
    const QString language = LanguageRegistry::languageForFile(path);
    if (language == LanguageRegistry::plainText)
        return;

    const BothSides sides = collectSides(lines_, fileStart, lines_[fileStart].location.file);
    const qsizetype size = sides.code[oldSide].size() + sides.code[newSide].size();
    if (sides.places.isEmpty() || size > std::min(budget_, maxHighlightedFileBytes))
        return;
    QsciScintilla *scratch = scratches_.forLanguage(language);
    if (!scratch)
        return;
    const std::array<QByteArray, 2> styles = scratchStyles(*scratch, sides.code);

    // The scratch editor's styles become the diff's, by their look.
    const long plain = scintillaColor(theme_.foreground);
    std::vector<int> styleOf(256, -1);
    const auto diffStyle = [&](int scratchStyle) {
        int &style = styleOf[scratchStyle & 0xff];
        if (style >= 0)
            return style;
        const long foreground = scratch->SendScintilla(QsciScintillaBase::SCI_STYLEGETFORE, scratchStyle);
        const bool bold = scratch->SendScintilla(QsciScintillaBase::SCI_STYLEGETBOLD, scratchStyle);
        const bool italic = scratch->SendScintilla(QsciScintillaBase::SCI_STYLEGETITALIC, scratchStyle);
        const bool isPlain = foreground == plain && !bold && !italic;
        style = isPlain ? plainStyle : syntaxStyle(foreground, bold, italic);
        return style;
    };
    for (const SidePlace &place : sides.places)
        styleRuns(place.line, QByteArrayView(styles[place.side]).sliced(place.offset), diffStyle);
    budget_ -= size;
}

void DiffSyntaxHighlighter::styleRuns(int line, QByteArrayView scratchStyles, const std::function<int(int)> &diffStyle)
{
    const long start = view_.SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, line);
    const long length = static_cast<long>(lines_[line].text.toUtf8().size());
    for (long run = 0; run < length;) {
        const char scratchStyle = scratchStyles[run];
        long end = run + 1;
        while (end < length && scratchStyles[end] == scratchStyle)
            ++end;
        if (const int style = diffStyle(static_cast<unsigned char>(scratchStyle)); style != plainStyle) {
            view_.SendScintilla(QsciScintillaBase::SCI_STARTSTYLING, start + run);
            view_.SendScintilla(QsciScintillaBase::SCI_SETSTYLING, end - run, style);
        }
        run = end;
    }
}

int DiffSyntaxHighlighter::syntaxStyle(long foreground, bool bold, bool italic)
{
    const qint64 key = (qint64(foreground) << 2) | (bold ? 2 : 0) | (italic ? 1 : 0);
    if (const auto it = syntaxStyles_.constFind(key); it != syntaxStyles_.constEnd())
        return it.value();
    const int style = firstSyntaxStyle + static_cast<int>(syntaxStyles_.size());
    if (style > lastSyntaxStyle)
        return plainStyle;
    const QByteArray family = theme_.font.family().toUtf8();
    view_.SendScintilla(QsciScintillaBase::SCI_STYLESETFORE, style, foreground);
    view_.SendScintilla(QsciScintillaBase::SCI_STYLESETBACK, style, theme_.background);
    view_.SendScintilla(QsciScintillaBase::SCI_STYLESETFONT, style, family.constData());
    view_.SendScintilla(QsciScintillaBase::SCI_STYLESETSIZEFRACTIONAL, style,
                        qRound(theme_.font.pointSizeF() * int(QsciScintillaBase::SC_FONT_SIZE_MULTIPLIER)));
    view_.SendScintilla(QsciScintillaBase::SCI_STYLESETWEIGHT, style, ScintillaStyles::styleWeight(theme_.font, bold));
    view_.SendScintilla(QsciScintillaBase::SCI_STYLESETITALIC, style, italic);
    syntaxStyles_.insert(key, style);
    return style;
}
