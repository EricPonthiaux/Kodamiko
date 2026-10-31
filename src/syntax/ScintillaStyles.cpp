// SPDX-License-Identifier: GPL-3.0-only

#include "ScintillaStyles.h"

#include "scintilla/ScintillaView.h"
#include "ui/AppFonts.h"
#include "ui/Theme.h"

#include <QFontInfo>
#include <QFontMetricsF>

#include <algorithm>

namespace ScintillaStyles {

void applyDefaultStyle(const ScintillaView &editor, const Theme &theme)
{
    // With the ligatures if the editor's are on (the highlighters style
    // the editors).
    const QByteArray family = AppFonts::scintillaFamily(theme.font, theme.editorLigatures);
    editor.sends(SCI_STYLESETFONT, STYLE_DEFAULT, family.constData());
    editor.send(SCI_STYLESETSIZE, STYLE_DEFAULT, theme.font.pointSize());
    editor.send(SCI_STYLESETWEIGHT, STYLE_DEFAULT, styleWeight(theme.font, false));
    editor.send(SCI_STYLESETFORE, STYLE_DEFAULT, scintillaColor(theme.foreground));
    editor.send(SCI_STYLESETBACK, STYLE_DEFAULT, scintillaColor(theme.background));
    editor.send(SCI_STYLECLEARALL);
}

void applySyntaxStyles(const ScintillaView &editor, const Theme &theme)
{
    const int count = std::min<int>(theme.syntaxStyles.size(), maxSyntaxStyles);
    for (int i = 0; i < count; ++i) {
        const TextStyle &style = theme.syntaxStyles[i].second;
        const unsigned long styleNumber = firstSyntaxStyle + i;
        editor.send(SCI_STYLESETFORE, styleNumber, scintillaColor(style.foreground));
        editor.send(SCI_STYLESETWEIGHT, styleNumber, styleWeight(theme.font, style.bold));
        editor.send(SCI_STYLESETITALIC, styleNumber, style.italic);
    }
}

void applyEditorColors(const ScintillaView &editor, const Theme &theme)
{
    // A translucent color lays over the text, an opaque one under it.
    const auto layerOf = [](const QColor &color) { return color.alpha() < 255 ? SC_LAYER_OVER_TEXT : SC_LAYER_BASE; };
    editor.send(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET, scintillaColorWithAlpha(theme.caret));
    editor.send(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK, scintillaColorWithAlpha(theme.caretLine));
    editor.send(SCI_SETCARETLINELAYER, layerOf(theme.caretLine));
    // The same selection with the focus elsewhere: Scintilla greys it.
    for (const int element : {SC_ELEMENT_SELECTION_BACK, SC_ELEMENT_SELECTION_ADDITIONAL_BACK,
                              SC_ELEMENT_SELECTION_SECONDARY_BACK, SC_ELEMENT_SELECTION_INACTIVE_BACK})
        editor.send(SCI_SETELEMENTCOLOUR, uptr_t(element), scintillaColorWithAlpha(theme.selection));
    editor.send(SCI_SETSELECTIONLAYER, layerOf(theme.selection));
    // The selection keeps the syntax colors.
    for (const int element : {SC_ELEMENT_SELECTION_TEXT, SC_ELEMENT_SELECTION_ADDITIONAL_TEXT,
                              SC_ELEMENT_SELECTION_SECONDARY_TEXT, SC_ELEMENT_SELECTION_INACTIVE_TEXT})
        editor.send(SCI_RESETELEMENTCOLOUR, uptr_t(element));

    const QByteArray family = AppFonts::scintillaFamily(theme.font, false);
    editor.sends(SCI_STYLESETFONT, STYLE_LINENUMBER, family.constData());
    editor.send(SCI_STYLESETSIZE, STYLE_LINENUMBER, theme.font.pointSize());
    editor.send(SCI_STYLESETFORE, STYLE_LINENUMBER, scintillaColor(theme.marginForeground));
    editor.send(SCI_STYLESETBACK, STYLE_LINENUMBER, scintillaColor(theme.marginBackground));
    editor.send(SCI_STYLESETBACK, STYLE_BRACELIGHT, scintillaColor(theme.braceMatch));
    editor.send(SCI_STYLESETFORE, STYLE_BRACELIGHT, scintillaColor(theme.foreground));
    editor.send(SCI_STYLESETFORE, STYLE_BRACEBAD, scintillaColor(theme.error));
    editor.send(SCI_STYLESETBACK, STYLE_INDENTGUIDE, scintillaColor(theme.background));
    editor.send(SCI_STYLESETFORE, STYLE_INDENTGUIDE, scintillaColor(theme.caretLine));
    editor.send(SCI_SETEDGECOLOUR, scintillaColor(theme.caretLine));
}

void applyTypography(const ScintillaView &editor, const QFont &font, qreal lineHeight)
{
    const qreal fontPixels = QFontInfo(font).pixelSize();
    const int extra = std::max(0, qRound(fontPixels * lineHeight - QFontMetricsF(font).height()));
    editor.send(SCI_SETEXTRAASCENT, extra / 2);
    editor.send(SCI_SETEXTRADESCENT, extra - extra / 2);
    editor.send(SCI_SETMARGINLEFT, 0, 8);
    editor.send(SCI_SETCARETWIDTH, 2);
}

int styleWeight(const QFont &font, bool bold)
{
    return bold ? std::max<int>(QFont::Bold, font.weight()) : font.weight();
}

} // namespace ScintillaStyles
