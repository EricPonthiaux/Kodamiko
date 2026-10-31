// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

class ScintillaView;
struct Theme;

// Highlights an editor's text. Each editor owns its own highlighter, chosen
// according to the file's language (see HighlighterFactory).
class ISyntaxHighlighter {
public:
    virtual ~ISyntaxHighlighter() = default;

    // Takes over highlighting of `editor`. Called only once.
    virtual void attach(ScintillaView *editor) = 0;

    // (Re)defines the Scintilla styles from the theme and recolors the text.
    virtual void applyTheme(const Theme &theme) = 0;

    // The theme's category ("comment", "string"…) that Scintilla style
    // `style` shows, or an empty string for the default text.
    virtual QString styleCategory(int style, const Theme &theme) const = 0;

    // Brings work deferred after an edit (fold levels) up to date, before
    // a command that relies on it. Pure, like the rest, so that a class can
    // implement this interface next to QObject.
    virtual void flush() = 0;
};
