// SPDX-License-Identifier: GPL-3.0-only

#include "DiffStyles.h"

#include "ui/AppFonts.h"

namespace DiffStyles {

namespace {

struct DiffColors {
    QColor added;
    QColor removed;
    QColor addedWords;
    QColor removedWords;
    QColor addedStrong;
    QColor removedStrong;
    QColor fileHeader;
    QColor hunkHeader;
    QColor filler;
    QColor muted;
};

struct StyleLook {
    QColor foreground;
    QColor background;
    QFont font;
};

// Title bars and separators in the interface's font, the code in the editor's.
struct DiffFonts {
    QFont label;
    QFont small;
    QFont name;
    QFont stats;
    QFont italic;
};

QColor mix(const QColor &a, const QColor &b, double share)
{
    return QColor::fromRgbF(a.redF() * (1 - share) + b.redF() * share, a.greenF() * (1 - share) + b.greenF() * share,
                            a.blueF() * (1 - share) + b.blueF() * share);
}

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
            mix(theme.background, theme.foreground, theme.isDark ? 0.035 : 0.03),
            mix(theme.background, theme.foreground, theme.isDark ? 0.02 : 0.025),
            theme.mutedText};
}

DiffFonts fontsFor(const Theme &theme)
{
    DiffFonts fonts;
    fonts.label = AppFonts::interfaceFont();
    fonts.label.setPointSizeF(theme.font.pointSizeF() * 0.95);
    fonts.small = fonts.label;
    fonts.small.setPointSizeF(fonts.label.pointSizeF() * 0.92);
    fonts.name = fonts.label;
    fonts.name.setBold(true);
    fonts.stats = theme.font;
    fonts.stats.setBold(true);
    fonts.italic = fonts.label;
    fonts.italic.setItalic(true);
    return fonts;
}

void defineStyle(const QsciScintilla &view, int style, const StyleLook &look)
{
    view.SendScintilla(QsciScintillaBase::SCI_STYLESETFORE, style, look.foreground);
    view.SendScintilla(QsciScintillaBase::SCI_STYLESETBACK, style, look.background);
    view.SendScintilla(QsciScintillaBase::SCI_STYLESETFONT, style, look.font.family().toUtf8().constData());
    view.SendScintilla(QsciScintillaBase::SCI_STYLESETSIZEFRACTIONAL, style,
                       qRound(look.font.pointSizeF() * int(QsciScintillaBase::SC_FONT_SIZE_MULTIPLIER)));
    view.SendScintilla(QsciScintillaBase::SCI_STYLESETWEIGHT, style, look.font.weight());
    view.SendScintilla(QsciScintillaBase::SCI_STYLESETITALIC, style, look.font.italic());
}

void defineTextStyles(const QsciScintilla &view, const Theme &theme, const DiffColors &colors)
{
    const DiffFonts fonts = fontsFor(theme);
    // The default style first: STYLECLEARALL copies it to every style.
    defineStyle(view, QsciScintillaBase::STYLE_DEFAULT, {theme.foreground, theme.background, theme.font});
    view.SendScintilla(QsciScintillaBase::SCI_STYLECLEARALL);
    defineStyle(view, plainStyle, {theme.foreground, theme.background, theme.font});
    defineStyle(view, fileHeaderStyle, {colors.muted, colors.fileHeader, fonts.label});
    defineStyle(view, fileNameStyle, {theme.foreground, colors.fileHeader, fonts.name});
    defineStyle(view, hunkHeaderStyle, {colors.muted, colors.hunkHeader, fonts.small});
    defineStyle(view, noteStyle, {colors.muted, theme.background, fonts.italic});
    defineStyle(view, numberStyle, {theme.marginForeground, theme.marginBackground, theme.font});
    defineStyle(view, addedNumberStyle, {colors.addedStrong, colors.added, theme.font});
    defineStyle(view, removedNumberStyle, {colors.removedStrong, colors.removed, theme.font});
    defineStyle(view, addedStatsStyle, {colors.addedStrong, colors.fileHeader, fonts.stats});
    defineStyle(view, removedStatsStyle, {colors.removedStrong, colors.fileHeader, fonts.stats});
    defineStyle(view, fileMarginStyle, {colors.muted, colors.fileHeader, theme.font});
    defineStyle(view, hunkMarginStyle, {colors.muted, colors.hunkHeader, theme.font});
    defineStyle(view, revealStyle, {theme.accent, colors.hunkHeader, fonts.name});
    view.SendScintilla(QsciScintillaBase::SCI_STYLESETHOTSPOT, revealStyle, 1L);
    view.SendScintilla(QsciScintillaBase::SCI_SETHOTSPOTACTIVEFORE, 1L, theme.foreground);
    view.SendScintilla(QsciScintillaBase::SCI_SETHOTSPOTACTIVEUNDERLINE, 0L);
    defineStyle(view, QsciScintillaBase::STYLE_LINENUMBER,
                {theme.marginForeground, theme.marginBackground, theme.font});
}

void defineMarker(QsciScintilla &view, QsciScintilla::MarkerSymbol symbol, int marker, const QColor &color)
{
    view.markerDefine(symbol, marker);
    view.setMarkerBackgroundColor(color, marker);
}

void defineMarkers(QsciScintilla &view, const DiffColors &colors)
{
    defineMarker(view, QsciScintilla::Background, addedBackground, colors.added);
    defineMarker(view, QsciScintilla::Background, removedBackground, colors.removed);
    defineMarker(view, QsciScintilla::Background, fileHeaderBackground, colors.fileHeader);
    defineMarker(view, QsciScintilla::Background, hunkHeaderBackground, colors.hunkHeader);
    defineMarker(view, QsciScintilla::Background, fillerBackground, colors.filler);
    defineMarker(view, QsciScintilla::FullRectangle, addedStrip, colors.addedStrong);
    defineMarker(view, QsciScintilla::FullRectangle, removedStrip, colors.removedStrong);
    defineMarker(view, QsciScintilla::FullRectangle, fileHeaderStrip, colors.fileHeader);
    defineMarker(view, QsciScintilla::FullRectangle, hunkHeaderStrip, colors.hunkHeader);
}

void defineWordsIndicator(const QsciScintilla &view, int indicator, const QColor &color)
{
    view.SendScintilla(QsciScintillaBase::SCI_INDICSETSTYLE, indicator, long(QsciScintillaBase::INDIC_FULLBOX));
    view.SendScintilla(QsciScintillaBase::SCI_INDICSETFORE, indicator, color);
    view.SendScintilla(QsciScintillaBase::SCI_INDICSETALPHA, indicator, 255L);
    view.SendScintilla(QsciScintillaBase::SCI_INDICSETOUTLINEALPHA, indicator, 0L);
    view.SendScintilla(QsciScintillaBase::SCI_INDICSETUNDER, indicator, 1L);
}

} // namespace

void define(QsciScintilla &view, const Theme &theme)
{
    const DiffColors colors = colorsFor(theme);
    defineTextStyles(view, theme, colors);
    defineMarkers(view, colors);
    defineWordsIndicator(view, addedWords, colors.addedWords);
    defineWordsIndicator(view, removedWords, colors.removedWords);
}

} // namespace DiffStyles
