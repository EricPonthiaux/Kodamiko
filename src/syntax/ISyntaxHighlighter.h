#pragma once

class QsciScintilla;
struct Theme;

// Highlights an editor's text. Each editor owns its own highlighter, chosen
// according to the file's language (see HighlighterFactory).
class ISyntaxHighlighter {
public:
    virtual ~ISyntaxHighlighter() = default;

    // Takes over highlighting of `editor`. Called only once.
    virtual void attach(QsciScintilla *editor) = 0;

    // (Re)defines the Scintilla styles from the theme and recolors the text.
    virtual void applyTheme(const Theme &theme) = 0;
};
