#pragma once

#include <QColor>
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
    QString name; // "dark", "purple", "light"
    QString title; // as shown in View ▸ Theme
    bool isDark = true;
    QFont font;

    QColor background;
    QColor foreground;
    QColor caret;
    QColor caretLine;
    QColor selection;
    QColor marginBackground;
    QColor marginForeground;
    QColor braceMatch;
    QColor findHighlight; // occurrences of a search

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

    static Theme dark();
    // Deep violet, the look of AI tools: dark plum background, lavender
    // text, violet accents and pastel syntax colors.
    static Theme purple();
    static Theme light();
    // Every theme, in menu order.
    static QList<Theme> all();
    // The theme called `name`; the dark one if there is none.
    static Theme named(const QString &name);

    // Index in `syntaxStyles` of the most specific style for a capture:
    // "function.method.builtin" tries "function.method.builtin", then
    // "function.method", then "function". Returns -1 if nothing matches.
    int syntaxStyleIndex(QStringView captureName) const;
};
