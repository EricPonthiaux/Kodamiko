#pragma once

class QFont;
class QsciScintilla;
struct Theme;

namespace ScintillaStyles {

// Scintilla style assigned to theme.syntaxStyles[i]: numbers 1..31 are free
// (0 = default text, 32 and above = styles reserved by Scintilla).
constexpr int firstSyntaxStyle = 1;
constexpr int maxSyntaxStyles = 31;

// Default font, text and background colors, copied into every style.
void applyDefaultStyle(QsciScintilla &editor, const Theme &theme);

// Syntax category styles (firstSyntaxStyle + index in the theme).
void applySyntaxStyles(QsciScintilla &editor, const Theme &theme);

// Lines set at AppFonts::codeLineHeight times the font size, some room
// between the margin and the text, a caret two pixels wide.
void applyTypography(QsciScintilla &editor, const QFont &font);

} // namespace ScintillaStyles
