#pragma once

class QsciScintilla;
struct Theme;

// Colore le texte d'un éditeur. Chaque éditeur possède son propre highlighter,
// choisi selon le langage du fichier (voir HighlighterFactory).
class ISyntaxHighlighter {
public:
    virtual ~ISyntaxHighlighter() = default;

    // Prend en charge la coloration de `editor`. Appelé une seule fois.
    virtual void attach(QsciScintilla *editor) = 0;

    // (Re)définit les styles Scintilla d'après le thème et recolore le texte.
    virtual void applyTheme(const Theme &theme) = 0;
};
