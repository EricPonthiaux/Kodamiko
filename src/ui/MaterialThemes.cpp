// SPDX-License-Identifier: GPL-3.0-only

#include "MaterialThemes.h"

#include "AppFonts.h"
#include "MaterialPalette.h"

#include <array>

// Each theme chooses its surfaces, its accent and eleven syntax colors from
// MaterialPalette; the roles they share follow from them (the caret takes the
// accent, the hovered rows the caret's line…), and the error, warning and
// find colors are the same in every dark theme and in every light one.

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
    bool isDark = true;
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
    theme.isDark = colors.isDark;
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
    theme.findHighlight = QColor(colors.isDark ? amber300 : amber800);
    theme.error = QColor(colors.isDark ? red400 : red700);
    theme.warning = QColor(colors.isDark ? amber600 : orange800);
    theme.window = QColor(colors.window);
    theme.button = QColor(colors.button);
    theme.alternateBase = QColor(colors.raised);
    theme.highlight = QColor(colors.highlight);
    theme.accent = QColor(colors.accent);
    theme.accentText = QColor(colors.isDark ? grey50 : grey900);
    theme.mutedText = QColor(colors.mutedText);
    theme.border = QColor(colors.border);
    theme.line = QColor(colors.line);
    theme.hover = QColor(colors.raised);
    theme.syntaxStyles = syntaxStyles(colors.syntax, colors.foreground);
    return theme;
}

// The dark themes: a neutral or a deep shade under the editor, the panels
// sunk below it, the syntax in the palette's light shades and accents.
// Indigo, Amethyst, Orchid, Ocean and Lagoon turn it around: the deep shade
// for the panels, a neutral one under the editor.
const std::array<ThemeColors, 10> darkThemes = {{
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
        .syntax = {blueGrey400, lightGreen300, cyan200, orange300, deepPurpleA100, teal200, lightBlue300, pink200,
                   orange200, amber200, blueGrey300},
    },
    {
        .name = "duet-graphite",
        .title = "Duet-Graphite",
        .window = black,
        .background = grey900,
        .raised = blueGrey900,
        .button = grey800,
        .line = grey800,
        .border = grey700,
        .selection = grey800,
        .highlight = blueGrey700,
        .accent = cyanA400,
        .foreground = grey200,
        .mutedText = grey500,
        .marginForeground = grey700,
        .currentLineNumber = grey400,
        .syntax = {grey500, lightGreen200, lime200, deepOrange200, amber400, cyan200, lightBlue200, red200, orange100,
                   amber100, grey400},
    },
    {
        .name = "duet-midnight",
        .title = "Duet-Midnight",
        .window = black,
        .background = black,
        .raised = grey900,
        .button = grey900,
        .line = grey900,
        .border = grey800,
        .selection = blueGrey800,
        .highlight = indigo800,
        .accent = indigoA100,
        .foreground = grey300,
        .mutedText = grey500,
        .marginForeground = grey700,
        .currentLineNumber = grey400,
        .syntax = {grey600, greenA100, cyanA100, amberA100, purpleA100, tealA100, blueA100, redA100, orangeA100,
                   yellowA100, grey500},
    },
    {
        .name = "duet-indigo",
        .title = "Duet-Indigo",
        .window = indigo900,
        .background = grey900,
        .raised = indigo800,
        .button = indigo800,
        .line = indigo800,
        .border = indigo700,
        .selection = indigo600,
        .highlight = indigo600,
        .accent = indigoA100,
        .foreground = indigo50,
        .mutedText = indigo200,
        .marginForeground = indigo400,
        .currentLineNumber = indigo100,
        .syntax = {indigo200, lightGreen200, cyanA100, amber300, pinkA100, tealA100, lightBlueA100, deepOrange200,
                   purple100, yellow200, indigo100},
    },
    {
        .name = "duet-amethyst",
        .title = "Duet-Amethyst",
        .window = deepPurple900,
        .background = grey900,
        .raised = deepPurple800,
        .button = deepPurple800,
        .line = deepPurple800,
        .border = deepPurple700,
        .selection = deepPurple600,
        .highlight = deepPurple600,
        .accent = deepPurpleA100,
        .foreground = deepPurple50,
        .mutedText = deepPurple200,
        .marginForeground = deepPurple400,
        .currentLineNumber = deepPurple100,
        .syntax = {deepPurple200, lightGreen200, cyanA100, orange200, pinkA100, tealA100, lightBlueA100, redA100,
                   amber200, yellow100, deepPurple100},
    },
    {
        .name = "duet-orchid",
        .title = "Duet-Orchid",
        .window = purple900,
        .background = grey900,
        .raised = purple800,
        .button = purple800,
        .line = purple800,
        .border = purple700,
        .selection = purple600,
        .highlight = purple700,
        .accent = purpleA100,
        .foreground = purple50,
        .mutedText = purple200,
        .marginForeground = purple300,
        .currentLineNumber = purple100,
        .syntax = {purple200, lightGreenA100, cyanA100, amberA100, pinkA100, tealA100, lightBlueA100, redA100,
                   orange100, yellow100, purple100},
    },
    {
        .name = "duet-ocean",
        .title = "Duet-Ocean",
        .window = blue900,
        .background = grey900,
        .raised = blue800,
        .button = blue800,
        .line = blue800,
        .border = blue700,
        .selection = blue600,
        .highlight = blue700,
        .accent = lightBlueA100,
        .foreground = blue50,
        .mutedText = blue200,
        .marginForeground = blue300,
        .currentLineNumber = blue100,
        .syntax = {blue200, lightGreenA100, cyanA100, amberA100, pinkA100, tealA100, yellow200, redA100, deepOrange100,
                   orange100, blue100},
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
        .syntax = {brown300, lightGreen300, cyan100, deepOrange200, amber400, teal200, yellow200, red200, orange100,
                   amber100, brown200},
    },
    {
        .name = "duet-ember",
        .title = "Duet-Ember",
        .window = black,
        .background = grey900,
        .raised = brown900,
        .button = grey800,
        .line = grey800,
        .border = grey800,
        .selection = brown800,
        .highlight = deepOrange900,
        .accent = deepOrangeA200,
        .foreground = grey200,
        .mutedText = grey500,
        .marginForeground = grey700,
        .currentLineNumber = deepOrange200,
        .syntax = {grey500, lime300, cyan200, orange300, deepOrange300, teal200, lightBlue200, red300, orange100,
                   amber200, grey400},
    },
}};

// The light themes: white or a pale shade under the editor, the panels in a
// tint of the theme's hue, the syntax in the palette's dark shades. Paper
// and Rose turn it around: white panels around a tinted editor.
const std::array<ThemeColors, 10> lightThemes = {{
    {
        .name = "duet-paper",
        .title = "Duet-Paper",
        .isDark = false,
        .window = white,
        .background = grey100,
        .raised = grey100,
        .button = white,
        .line = grey300,
        .border = grey400,
        .selection = indigo100,
        .highlight = indigo500,
        .accent = indigo500,
        .foreground = grey900,
        .mutedText = grey700,
        .marginForeground = grey500,
        .currentLineNumber = grey800,
        .syntax = {grey600, green800, cyan800, deepOrange800, purple700, teal700, blue700, red700, pink700, brown600,
                   grey700},
    },
    {
        .name = "duet-cloud",
        .title = "Duet-Cloud",
        .isDark = false,
        .window = blueGrey100,
        .background = blueGrey50,
        .raised = white,
        .button = white,
        .line = blueGrey100,
        .border = blueGrey200,
        .selection = lightBlue100,
        .highlight = blueGrey600,
        .accent = lightBlue700,
        .foreground = blueGrey900,
        .mutedText = blueGrey600,
        .marginForeground = blueGrey300,
        .currentLineNumber = blueGrey800,
        .syntax = {blueGrey500, green800, teal700, deepOrange800, indigo600, cyan800, lightBlue800, pink700, brown600,
                   purple700, blueGrey700},
    },
    {
        .name = "duet-sky",
        .title = "Duet-Sky",
        .isDark = false,
        .window = lightBlue50,
        .background = white,
        .raised = lightBlue50,
        .button = white,
        .line = lightBlue100,
        .border = lightBlue200,
        .selection = lightBlue100,
        .highlight = lightBlue800,
        .accent = lightBlue700,
        .foreground = blueGrey900,
        .mutedText = blueGrey600,
        .marginForeground = lightBlue300,
        .currentLineNumber = blueGrey800,
        .syntax = {blueGrey500, lightGreen900, teal700, deepOrange800, indigo500, cyan800, blue800, pink600,
                   deepPurple500, brown600, blueGrey700},
    },
    {
        .name = "duet-mint",
        .title = "Duet-Mint",
        .isDark = false,
        .window = teal50,
        .background = white,
        .raised = teal50,
        .button = white,
        .line = teal100,
        .border = teal200,
        .selection = teal100,
        .highlight = teal600,
        .accent = teal600,
        .foreground = grey900,
        .mutedText = blueGrey600,
        .marginForeground = teal300,
        .currentLineNumber = teal900,
        .syntax = {blueGrey500, green800, cyan800, deepOrange800, purple700, teal800, blue700, red700, pink700,
                   brown600, blueGrey700},
    },
    {
        .name = "duet-sage",
        .title = "Duet-Sage",
        .isDark = false,
        .window = lightGreen50,
        .background = white,
        .raised = lightGreen50,
        .button = white,
        .line = lightGreen100,
        .border = lightGreen200,
        .selection = lightGreen100,
        .highlight = lightGreen800,
        .accent = green700,
        .foreground = grey900,
        .mutedText = grey700,
        .marginForeground = green300,
        .currentLineNumber = green900,
        .syntax = {grey600, lightGreen900, teal700, deepOrange800, indigo600, teal800, blue700, red700, purple700,
                   brown600, grey700},
    },
    {
        .name = "duet-lavender",
        .title = "Duet-Lavender",
        .isDark = false,
        .window = deepPurple50,
        .background = white,
        .raised = deepPurple50,
        .button = white,
        .line = deepPurple100,
        .border = deepPurple200,
        .selection = deepPurple100,
        .highlight = deepPurple500,
        .accent = deepPurple500,
        .foreground = grey900,
        .mutedText = deepPurple400,
        .marginForeground = deepPurple200,
        .currentLineNumber = deepPurple900,
        .syntax = {grey600, teal700, cyan800, pink700, deepPurple600, indigo600, blue700, red700, purple600, brown600,
                   grey700},
    },
    {
        .name = "duet-rose",
        .title = "Duet-Rose",
        .isDark = false,
        .window = white,
        .background = pink50,
        .raised = pink50,
        .button = white,
        .line = pink100,
        .border = pink200,
        .selection = pink100,
        .highlight = pink600,
        .accent = pink600,
        .foreground = grey900,
        .mutedText = grey700,
        .marginForeground = pink200,
        .currentLineNumber = pink900,
        .syntax = {grey600, green800, teal700, deepOrange800, pink700, purple700, indigo500, red800, deepPurple400,
                   brown600, grey700},
    },
    {
        .name = "duet-peach",
        .title = "Duet-Peach",
        .isDark = false,
        .window = orange50,
        .background = white,
        .raised = orange50,
        .button = white,
        .line = orange100,
        .border = orange200,
        .selection = deepOrange100,
        .highlight = deepOrange600,
        .accent = deepOrange600,
        .foreground = brown900,
        .mutedText = brown500,
        .marginForeground = orange300,
        .currentLineNumber = brown800,
        .syntax = {brown400, green800, teal700, deepOrange900, indigo600, cyan800, blue700, red800, purple600, brown600,
                   brown700},
    },
    {
        .name = "duet-sand",
        .title = "Duet-Sand",
        .isDark = false,
        .window = brown50,
        .background = amber50,
        .raised = brown50,
        .button = amber50,
        .line = brown100,
        .border = brown200,
        .selection = orange100,
        .highlight = brown500,
        .accent = orange800,
        .foreground = brown900,
        .mutedText = brown600,
        .marginForeground = brown300,
        .currentLineNumber = brown800,
        .syntax = {brown400, green800, teal700, deepOrange800, purple800, teal800, blue800, red800, orange900, brown600,
                   brown700},
    },
    {
        .name = "duet-lemon",
        .title = "Duet-Lemon",
        .isDark = false,
        .window = yellow100,
        .background = yellow50,
        .raised = yellow100,
        .button = yellow50,
        .line = yellow200,
        .border = amber200,
        .selection = amber100,
        .highlight = lime900,
        .accent = amber900,
        .foreground = grey900,
        .mutedText = grey700,
        .marginForeground = grey500,
        .currentLineNumber = grey800,
        .syntax = {grey600, green800, teal700, deepOrange800, blue800, cyan800, indigo600, red700, purple700, brown600,
                   grey700},
    },
}};

} // namespace

namespace MaterialThemes {

QList<Theme> all()
{
    QList<Theme> themes;
    for (const ThemeColors &colors : darkThemes)
        themes.append(themeFrom(colors));
    for (const ThemeColors &colors : lightThemes)
        themes.append(themeFrom(colors));
    return themes;
}

} // namespace MaterialThemes
