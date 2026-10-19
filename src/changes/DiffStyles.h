#pragma once

#include "ui/Theme.h"

#include <Qsci/qsciscintilla.h>

// The Scintilla styles, markers, indicators and margins of a DiffView.
namespace DiffStyles {

// Styles of the text (no lexer: they are set by hand) and of the margin;
// from firstSyntaxStyle, the colors of the code (see DiffSyntaxHighlighter).
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

// Defines the styles, markers and indicators above in `view`, in `theme`'s
// colors and fonts. Resets every style: the text must be styled again.
void define(QsciScintilla &view, const Theme &theme);

} // namespace DiffStyles
