#pragma once

#include <QColor>
#include "AppFonts.h"

#include <QFont>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <utility>

// Style of a syntax category (keyword, string, comment...).
struct TextStyle {
    QColor foreground;
    bool bold = false;
    bool italic = false;
};

// Complete editor palette: text UI colors and syntax category styles.
// Categories are named after tree-sitter captures ("keyword",
// "string.special", "function.method"...).
struct Theme {
    QString name; // "duet", "dark", "purple", "light"
    QString title; // as shown in View ▸ Theme
    bool isDark = true;
    // The code font, without ligatures; its weight is the user's choice.
    QFont font;
    // The code's lines, at this multiple of the font size (not in the
    // terminal, whose cells follow the font).
    qreal lineHeight = AppFonts::defaultLineHeight;
    // Programming ligatures, in the editor only.
    bool editorLigatures = false;

    QColor background;
    QColor foreground;
    QColor caret;
    QColor caretLine;
    QColor selection;
    QColor marginBackground;
    QColor marginForeground;
    QColor currentLineNumber; // the caret's line, brighter than the others
    QColor braceMatch;
    QColor findHighlight; // occurrences of a search
    QColor error;   // diagnostics' squiggles, unmatched braces
    QColor warning;

    // Interface around the editor: the application's palette and the
    // controls' style sheet (see ThemeStyle).
    QColor window;     // panels, menu bar, status bar
    QColor button;
    QColor alternateBase;
    QColor highlight;  // selection in lists
    QColor accent;     // focus, current tab, links
    QColor accentText; // text of the current tab, of checked buttons
    QColor mutedText;  // inactive tabs, headers, status bar
    QColor border;     // fields, buttons, menus, scroll bar handles
    QColor line;       // separators
    QColor hover;      // rows under the mouse
    QColor gradientStart; // selected menu items and rows, default buttons
    QColor gradientEnd;

    QList<std::pair<QString, TextStyle>> syntaxStyles;

    // The default, on the brand's colors: the dark slate of the icon's
    // tile, its blue as the accent and its white as the caret; flat
    // selections and panels a level below the editor.
    static Theme duet();
    static Theme dark();
    // Deep violet, the look of AI tools: dark plum background, lavender
    // text, violet accents and pastel syntax colors.
    static Theme purple();
    static Theme light();
    // Every theme, in menu order.
    static QList<Theme> all();
    // The theme called `name`; Duet if there is none.
    static Theme named(const QString &name);

    // Index in `syntaxStyles` of the most specific style for a capture:
    // "function.method.builtin" tries "function.method.builtin", then
    // "function.method", then "function". Returns -1 if nothing matches.
    int syntaxStyleIndex(QStringView captureName) const;
};
