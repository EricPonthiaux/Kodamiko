#include "ScintillaStyles.h"

#include "ui/AppFonts.h"
#include "ui/Theme.h"

#include <QFontInfo>
#include <QFontMetricsF>
#include <Qsci/qsciscintilla.h>

#include <algorithm>

namespace ScintillaStyles {

void applyDefaultStyle(QsciScintilla &editor, const Theme &theme)
{
    // With the ligatures if the editor's are on (the highlighters style
    // the editors).
    const QByteArray family = AppFonts::scintillaFamily(theme.font, theme.editorLigatures);
    editor.SendScintilla(QsciScintillaBase::SCI_STYLESETFONT, QsciScintillaBase::STYLE_DEFAULT, family.constData());
    editor.SendScintilla(QsciScintillaBase::SCI_STYLESETSIZE, QsciScintillaBase::STYLE_DEFAULT,
                         static_cast<long>(theme.font.pointSize()));
    editor.SendScintilla(QsciScintillaBase::SCI_STYLESETWEIGHT, QsciScintillaBase::STYLE_DEFAULT,
                         static_cast<long>(styleWeight(theme.font, false)));
    editor.SendScintilla(QsciScintillaBase::SCI_STYLESETFORE, QsciScintillaBase::STYLE_DEFAULT, theme.foreground);
    editor.SendScintilla(QsciScintillaBase::SCI_STYLESETBACK, QsciScintillaBase::STYLE_DEFAULT, theme.background);
    editor.SendScintilla(QsciScintillaBase::SCI_STYLECLEARALL);
}

void applySyntaxStyles(QsciScintilla &editor, const Theme &theme)
{
    const int count = std::min<int>(theme.syntaxStyles.size(), maxSyntaxStyles);
    for (int i = 0; i < count; ++i) {
        const TextStyle &style = theme.syntaxStyles[i].second;
        const unsigned long styleNumber = firstSyntaxStyle + i;
        editor.SendScintilla(QsciScintillaBase::SCI_STYLESETFORE, styleNumber, style.foreground);
        editor.SendScintilla(QsciScintillaBase::SCI_STYLESETWEIGHT, styleNumber,
                             static_cast<long>(styleWeight(theme.font, style.bold)));
        editor.SendScintilla(QsciScintillaBase::SCI_STYLESETITALIC, styleNumber, static_cast<long>(style.italic));
    }
}

void applyTypography(QsciScintilla &editor, const QFont &font, qreal lineHeight)
{
    const qreal fontPixels = QFontInfo(font).pixelSize();
    const int extra = std::max(0, qRound(fontPixels * lineHeight - QFontMetricsF(font).height()));
    editor.SendScintilla(QsciScintillaBase::SCI_SETEXTRAASCENT, extra / 2);
    editor.SendScintilla(QsciScintillaBase::SCI_SETEXTRADESCENT, extra - extra / 2);
    editor.SendScintilla(QsciScintillaBase::SCI_SETMARGINLEFT, 0, 8L);
    editor.SendScintilla(QsciScintillaBase::SCI_SETCARETWIDTH, 2);
}

int styleWeight(const QFont &font, bool bold)
{
    return bold ? std::max<int>(QFont::Bold, font.weight()) : font.weight();
}

} // namespace ScintillaStyles
