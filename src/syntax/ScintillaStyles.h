#pragma once

class QsciScintilla;
struct Theme;

namespace ScintillaStyles {

// Style Scintilla attribué à theme.syntaxStyles[i] : les numéros 1..31 sont
// libres (0 = texte par défaut, 32 et plus = styles réservés par Scintilla).
constexpr int firstSyntaxStyle = 1;
constexpr int maxSyntaxStyles = 31;

// Police, couleurs de texte et de fond par défaut, copiées dans tous les styles.
void applyDefaultStyle(QsciScintilla &editor, const Theme &theme);

// Styles des catégories syntaxiques (firstSyntaxStyle + index dans le thème).
void applySyntaxStyles(QsciScintilla &editor, const Theme &theme);

} // namespace ScintillaStyles
