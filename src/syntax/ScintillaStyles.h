// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtGlobal>

class QFont;
class ScintillaView;
struct Theme;

namespace ScintillaStyles {

// Scintilla style assigned to theme.syntaxStyles[i]: numbers 1..31 are free
// (0 = default text, 32 and above = styles reserved by Scintilla).
constexpr int firstSyntaxStyle = 1;
constexpr int maxSyntaxStyles = 31;

// Default font, text and background colors, copied into every style.
void applyDefaultStyle(const ScintillaView &editor, const Theme &theme);

// Syntax category styles (firstSyntaxStyle + index in the theme).
void applySyntaxStyles(const ScintillaView &editor, const Theme &theme);

// The editor's caret, caret line, selection, margins, braces, indentation
// guides and column limit in the theme's colors.
void applyEditorColors(const ScintillaView &editor, const Theme &theme);

// Lines set at `lineHeight` times the font size, some room between the
// margin and the text, a caret two pixels wide.
void applyTypography(const ScintillaView &editor, const QFont &font, qreal lineHeight);

// The weight of a style in `font`'s weight, bold or not: bold stays bold
// whatever the weight chosen for the code (Light, Medium…).
int styleWeight(const QFont &font, bool bold);

} // namespace ScintillaStyles
