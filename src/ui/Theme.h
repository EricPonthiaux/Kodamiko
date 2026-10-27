// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "AppFonts.h"

#include <QColor>
#include <QFont>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <utility>

struct TextStyle {
    QColor foreground;
    bool bold = false;
    bool italic = false;
};

struct Theme {
    QString name;  // "duet", "duet-night", "duet-slate"…
    QString title; // as shown in View ▸ Theme
    bool isDark = true;
    QFont font;
    qreal lineHeight = AppFonts::defaultLineHeight;
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
    QColor error;         // diagnostics' squiggles, unmatched braces
    QColor warning;
    QColor window; // panels, menu bar, status bar
    QColor button;
    QColor alternateBase;
    QColor highlight;  // selection in lists
    QColor accent;     // focus, current tab, links
    QColor accentText; // text of the current tab, of checked buttons
    QColor mutedText;  // inactive tabs, headers, status bar
    QColor border;     // fields, buttons, menus, scroll bar handles
    QColor line;       // separators
    QColor hover;      // rows under the mouse
    // Text and check marks drawn over the accent.
    QColor onAccent{Qt::white};
    // Claude's own orange, the same in every theme: what Claude wrote.
    QColor claude{0xd9, 0x77, 0x57};

    QList<std::pair<QString, TextStyle>> syntaxStyles;

    static Theme duet();

    // Duet's theme, the website's (Duet-Night), Duet-Violet, Duet-Stratos,
    // Duet-Bloom, then the Material ones (MaterialThemes).
    static QList<Theme> all();

    static Theme named(const QString &name);

    int syntaxStyleIndex(QStringView captureName) const;
};
