// SPDX-License-Identifier: GPL-3.0-only

#include "MaterialThemes.h"

#include "AppFonts.h"
#include "MaterialPalette.h"

#include <array>

// Each theme chooses its surfaces, its accent and eleven syntax colors from
// MaterialPalette; the roles they share follow from them (the caret takes the
// accent, the hovered rows the caret's line…), and the error, warning and
// find colors are the same in every theme.

namespace {

using namespace MaterialPalette;

struct SyntaxColors {
    const char *comment = nullptr;
    const char *string = nullptr;
    const char *escape = nullptr; // escapes, operators, built-in functions
    const char *number = nullptr; // numbers and constants
    const char *keyword = nullptr;
    const char *type = nullptr; // types, constructors, modules
    const char *function = nullptr;
    const char *special = nullptr; // built-in variables, labels, tags
    const char *parameter = nullptr;
    const char *property = nullptr; // properties and attributes
    const char *punctuation = nullptr;
};

struct ThemeColors {
    const char *name = nullptr;
    const char *title = nullptr;
    const char *window = nullptr;
    const char *background = nullptr;
    const char *raised = nullptr; // the caret's line, the hovered rows, the popups
    const char *button = nullptr;
    const char *line = nullptr;
    const char *border = nullptr; // also the matching brace
    const char *selection = nullptr;
    const char *highlight = nullptr; // under white text
    const char *accent = nullptr;
    const char *foreground = nullptr;
    const char *mutedText = nullptr;
    const char *marginForeground = nullptr;
    const char *currentLineNumber = nullptr;
    SyntaxColors syntax = {};
};

TextStyle plain(const char *color)
{
    return {QColor(color)};
}

TextStyle italic(const char *color)
{
    return {QColor(color), false, true};
}

TextStyle bold(const char *color)
{
    return {QColor(color), true, false};
}

QList<std::pair<QString, TextStyle>> syntaxStyles(const SyntaxColors &colors, const char *foreground)
{
    return {
        {"comment", italic(colors.comment)},
        {"string", plain(colors.string)},
        {"string.special", plain(colors.escape)},
        {"escape", plain(colors.escape)},
        {"number", plain(colors.number)},
        {"constant", plain(colors.number)},
        {"constant.builtin", plain(colors.number)},
        {"keyword", plain(colors.keyword)},
        {"operator", plain(colors.escape)},
        {"type", plain(colors.type)},
        {"type.builtin", plain(colors.type)},
        {"constructor", plain(colors.type)},
        {"module", plain(colors.type)},
        {"function", plain(colors.function)},
        {"function.builtin", plain(colors.escape)},
        {"variable", plain(foreground)},
        {"variable.builtin", plain(colors.special)},
        {"variable.parameter", italic(colors.parameter)},
        {"property", plain(colors.property)},
        {"label", plain(colors.special)},
        {"attribute", plain(colors.property)},
        {"tag", plain(colors.special)},
        {"punctuation", plain(colors.punctuation)},
        {"punctuation.special", plain(colors.keyword)},
        {"embedded", plain(foreground)},
        {"heading", bold(colors.function)},
        {"diff.plus", plain(colors.string)},
        {"diff.minus", plain(colors.special)},
    };
}

Theme themeFrom(const ThemeColors &colors)
{
    Theme theme;
    theme.name = QString::fromLatin1(colors.name);
    theme.title = QString::fromLatin1(colors.title);
    theme.isDark = true;
    theme.font = AppFonts::codeFont(11);
    theme.background = QColor(colors.background);
    theme.foreground = QColor(colors.foreground);
    theme.caret = QColor(colors.accent);
    theme.caretLine = QColor(colors.raised);
    theme.selection = QColor(colors.selection);
    theme.marginBackground = QColor(colors.background);
    theme.marginForeground = QColor(colors.marginForeground);
    theme.currentLineNumber = QColor(colors.currentLineNumber);
    theme.braceMatch = QColor(colors.border);
    theme.findHighlight = QColor(amber300);
    theme.error = QColor(red400);
    theme.warning = QColor(amber600);
    theme.window = QColor(colors.window);
    theme.button = QColor(colors.button);
    theme.alternateBase = QColor(colors.raised);
    theme.highlight = QColor(colors.highlight);
    theme.accent = QColor(colors.accent);
    theme.accentText = QColor(grey50);
    // The accents are light shades: white on them is unreadable.
    theme.onAccent = QColor(black);
    theme.mutedText = QColor(colors.mutedText);
    theme.border = QColor(colors.border);
    theme.line = QColor(colors.line);
    theme.hover = QColor(colors.raised);
    theme.syntaxStyles = syntaxStyles(colors.syntax, colors.foreground);
    return theme;
}

// The dark themes: a neutral or a deep shade under the editor, the panels
// sunk below it, the syntax in the palette's light shades and accents.
// Lagoon turns it around: the deep shade for the panels, a neutral one under
// the editor.
//
// Nocturne, Fjord, Tawny, Dusk and Lantern follow free VS Code themes: each
// of the original's colors replaced by the closest one of the palette
// (CIEDE2000), or, where that one was unreadable, the closest readable shade
// of its hue.
const std::array<ThemeColors, 8> themes = {{
    {
        .name = "duet-slate",
        .title = "Duet-Slate",
        .window = grey900,
        .background = blueGrey900,
        .raised = blueGrey800,
        .button = blueGrey800,
        .line = blueGrey800,
        .border = blueGrey700,
        .selection = blueGrey700,
        .highlight = lightBlue900,
        .accent = lightBlueA200,
        .foreground = blueGrey50,
        .mutedText = blueGrey300,
        .marginForeground = blueGrey600,
        .currentLineNumber = blueGrey200,
        .syntax = {blueGrey200, lightGreen300, cyan200, orange300, deepPurple100, teal200, lightBlue200, pink100,
                   orange200, amber200, blueGrey100},
    },
    {
        .name = "duet-lagoon",
        .title = "Duet-Lagoon",
        .window = teal900,
        .background = grey900,
        .raised = teal800,
        .button = teal800,
        .line = teal800,
        .border = teal700,
        .selection = teal600,
        .highlight = teal700,
        .accent = tealA200,
        .foreground = teal50,
        .mutedText = teal200,
        .marginForeground = teal400,
        .currentLineNumber = teal100,
        .syntax = {teal200, lightGreenA100, cyanA100, amber300, pink100, tealA100, lightBlueA100, deepOrange200,
                   orange100, amber100, teal100},
    },
    {
        .name = "duet-cocoa",
        .title = "Duet-Cocoa",
        .window = grey900,
        .background = brown900,
        .raised = brown800,
        .button = brown800,
        .line = brown800,
        .border = brown700,
        .selection = brown600,
        .highlight = brown600,
        .accent = orange300,
        .foreground = brown50,
        .mutedText = brown200,
        .marginForeground = brown400,
        .currentLineNumber = brown100,
        .syntax = {brown200, lightGreen300, cyan100, deepOrange200, amber400, teal200, yellow200, red100, orange100,
                   amber100, brown100},
    },
    // After Dracula (MIT, Copyright (c) 2016 Dracula Theme).
    {
        .name = "duet-nocturne",
        .title = "Duet-Nocturne",
        .window = grey900,
        .background = grey900,
        .raised = grey800,
        .button = grey900,
        .line = grey800,
        .border = grey700,
        .selection = grey800,
        .highlight = grey800,
        .accent = pinkA100,
        .foreground = grey50,
        .mutedText = indigo300,
        .marginForeground = indigo400,
        .currentLineNumber = grey50,
        .syntax = {indigoA100, limeA100, pink200, deepPurple100, pink200, cyan200, greenA400, deepPurple100, orange300,
                   grey50, grey50},
    },
    // After Nord (MIT, Copyright (c) 2016-present Sven Greb).
    {
        .name = "duet-fjord",
        .title = "Duet-Fjord",
        .window = blueGrey900,
        .background = blueGrey900,
        .raised = blueGrey800,
        .button = blueGrey800,
        .line = blueGrey800,
        .border = blueGrey700,
        .selection = blueGrey800,
        .highlight = lightBlue800,
        .accent = lightBlue200,
        .foreground = indigo50,
        .mutedText = blueGrey100,
        .marginForeground = blueGrey700,
        .currentLineNumber = indigo50,
        .syntax = {indigo200, green200, orangeA100, purple100, indigo100, teal200, lightBlue200, indigo100, indigo50,
                   indigo50, blueGrey50},
    },
    // After Night Owl (MIT, Copyright (c) 2018 Sarah Drasner).
    {
        .name = "duet-tawny",
        .title = "Duet-Tawny",
        .window = blueGrey900,
        .background = blueGrey900,
        .raised = grey900,
        .button = blueGrey900,
        .line = grey900,
        .border = blueGrey400,
        .selection = blueGrey800,
        .highlight = blueGrey800,
        .accent = deepPurple300,
        .foreground = indigo50,
        .mutedText = blueGrey300,
        .marginForeground = blueGrey600,
        .currentLineNumber = blue100,
        .syntax = {blueGrey200, orange200, deepOrange200, deepOrange200, purple100, lime300, blue200, teal200, teal200,
                   teal100, indigo50},
    },
    // After Palenight (MIT, Copyright (c) 2017-present Olaolu Olawuyi).
    {
        .name = "duet-dusk",
        .title = "Duet-Dusk",
        .window = blueGrey900,
        .background = blueGrey900,
        .raised = grey900,
        .button = blueGrey900,
        .line = grey900,
        .border = deepPurple400,
        .selection = blueGrey800,
        .highlight = deepPurple400,
        .accent = deepPurple300,
        .foreground = blueGrey200,
        .mutedText = indigo200,
        .marginForeground = indigo500,
        .currentLineNumber = teal50,
        .syntax = {indigo200, lightGreen300, deepOrange200, deepOrange200, purple100, orangeA100, blue200, pink100,
                   lightBlueA100, lightBlueA100, blueGrey200},
    },
    // After Tokyo Night (MIT, enkia).
    {
        .name = "duet-lantern",
        .title = "Duet-Lantern",
        .window = grey900,
        .background = grey900,
        .raised = blueGrey900,
        .button = grey800,
        .line = black,
        .border = blueGrey800,
        .selection = blueGrey900,
        .highlight = blue900,
        .accent = indigo300,
        .foreground = indigo200,
        .mutedText = indigo300,
        .marginForeground = blueGrey700,
        .currentLineNumber = indigo300,
        .syntax = {indigoA100, lightGreen400, lightBlueA100, deepOrangeA100, deepPurple100, indigo100, blueA100,
                   pink200, orange300, lightBlue200, lightBlueA100},
    },
}};

} // namespace

namespace MaterialThemes {

QList<Theme> all()
{
    QList<Theme> materialThemes;
    for (const ThemeColors &colors : themes)
        materialThemes.append(themeFrom(colors));
    return materialThemes;
}

} // namespace MaterialThemes
