// SPDX-License-Identifier: GPL-3.0-only

#include "Theme.h"

#include "AppFonts.h"
#include "KdeThemes.h"
#include "VsCodeThemes.h"

namespace {

QFont editorFont()
{
    return AppFonts::codeFont(11);
}

TextStyle plain(const char *color)
{
    return {QColor(color)};
}

TextStyle bold(const char *color)
{
    return {QColor(color), true, false};
}

TextStyle italic(const char *color)
{
    return {QColor(color), false, true};
}

} // namespace

Theme Theme::duet()
{
    Theme theme;
    theme.name = QStringLiteral("duet");
    theme.title = QStringLiteral("Duet");
    theme.font = editorFont();
    theme.background = QColor("#1f232c");
    theme.foreground = QColor("#c9cfdb");
    theme.caret = QColor("#e7e9ef");
    theme.caretLine = QColor("#252a35");
    theme.selection = QColor("#2c3c63");
    theme.marginBackground = QColor("#1f232c");
    theme.marginForeground = QColor("#4d5567");
    theme.currentLineNumber = QColor("#a3abbb");
    theme.braceMatch = QColor("#3a4358");
    theme.findHighlight = QColor("#e6c384");
    theme.error = QColor("#e5534b");
    theme.warning = QColor("#d4a72c");
    // Three levels: the panels sunk below the editor, the menus and popups
    // raised above it.
    theme.window = QColor("#181b22");
    theme.button = QColor("#2a2f3b");
    theme.alternateBase = QColor("#262b36");
    theme.highlight = QColor("#34539e");
    theme.accent = QColor("#4f7cf0");
    theme.accentText = QColor("#eef1f7");
    theme.mutedText = QColor("#808999");
    theme.border = QColor("#353b49");
    theme.line = QColor("#2a2f3a");
    theme.hover = QColor("#262b35");
    // Flat: the same color at both ends, a shade darker than the accent for
    // white text.
    theme.syntaxStyles = {
        {"comment", italic("#6b7489")},         {"string", plain("#a8d67e")},
        {"string.special", plain("#5fd4e8")},   {"escape", plain("#5fd4e8")},
        {"number", plain("#ff9e64")},           {"constant", plain("#ff9e64")},
        {"constant.builtin", plain("#ff9e64")}, {"keyword", plain("#a08cff")},
        {"operator", plain("#7fb5e0")},         {"type", plain("#4fd1c5")},
        {"type.builtin", plain("#4fd1c5")},     {"constructor", plain("#4fd1c5")},
        {"module", plain("#4fd1c5")},           {"function", plain("#6cb6ff")},
        {"function.builtin", plain("#5fd4e8")}, {"variable", plain("#c9cfdb")},
        {"variable.builtin", plain("#ff7a93")}, {"variable.parameter", italic("#f0a6ca")},
        {"property", plain("#e6c384")},         {"label", plain("#ff7a93")},
        {"attribute", plain("#e6c384")},        {"tag", plain("#ff7a93")},
        {"punctuation", plain("#8a93a6")},      {"punctuation.special", plain("#a08cff")},
        {"embedded", plain("#c9cfdb")},         {"heading", bold("#6cb6ff")},
        {"diff.plus", plain("#8fd18a")},        {"diff.minus", plain("#ff7a93")},
    };
    return theme;
}

Theme Theme::dark()
{
    Theme theme;
    theme.name = QStringLiteral("dark");
    theme.title = QStringLiteral("Dark");
    theme.font = editorFont();
    theme.background = QColor("#1f2329");
    theme.foreground = QColor("#abb2bf");
    theme.caret = QColor("#528bff");
    theme.caretLine = QColor("#2c313a");
    theme.selection = QColor("#3e4451");
    theme.marginBackground = QColor("#1f2329");
    theme.marginForeground = QColor("#5c6370");
    theme.currentLineNumber = QColor("#abb2bf");
    theme.braceMatch = QColor("#515a6b");
    theme.findHighlight = QColor("#e5c07b");
    theme.error = QColor("#e5534b");
    theme.warning = QColor("#d4a72c");
    theme.window = QColor("#21252b");
    theme.button = QColor("#2c313a");
    theme.alternateBase = QColor("#262a31");
    theme.highlight = QColor("#3e5f8a");
    theme.accent = QColor("#528bff");
    theme.accentText = QColor("#e6edf8");
    theme.mutedText = QColor("#7f848e");
    theme.border = QColor("#3b414d");
    theme.line = QColor("#2c313a");
    theme.hover = QColor("#262a31");
    theme.syntaxStyles = {
        {"comment", italic("#7f848e")},         {"string", plain("#98c379")},
        {"string.special", plain("#56b6c2")},   {"escape", plain("#56b6c2")},
        {"number", plain("#d19a66")},           {"constant", plain("#d19a66")},
        {"constant.builtin", plain("#d19a66")}, {"keyword", plain("#c678dd")},
        {"operator", plain("#56b6c2")},         {"type", plain("#e5c07b")},
        {"type.builtin", plain("#e5c07b")},     {"constructor", plain("#e5c07b")},
        {"module", plain("#e5c07b")},           {"function", plain("#61afef")},
        {"function.builtin", plain("#56b6c2")}, {"variable", plain("#abb2bf")},
        {"variable.builtin", plain("#e06c75")}, {"variable.parameter", plain("#d19a66")},
        {"property", plain("#e06c75")},         {"label", plain("#e06c75")},
        {"attribute", plain("#d19a66")},        {"tag", plain("#e06c75")},
        {"punctuation", plain("#abb2bf")},      {"punctuation.special", plain("#c678dd")},
        {"embedded", plain("#abb2bf")},         {"heading", bold("#e06c75")},
        {"diff.plus", plain("#98c379")},        {"diff.minus", plain("#e06c75")},
    };
    return theme;
}

Theme Theme::purple()
{
    Theme theme;
    theme.name = QStringLiteral("purple");
    theme.title = QStringLiteral("Purple");
    theme.font = editorFont();
    theme.background = QColor("#17131f");
    theme.foreground = QColor("#ddd6f3");
    theme.caret = QColor("#c4b5fd");
    theme.caretLine = QColor("#221c30");
    theme.selection = QColor("#3b2f5e");
    theme.marginBackground = QColor("#17131f");
    theme.marginForeground = QColor("#5f5680");
    theme.currentLineNumber = QColor("#b3a8d6");
    theme.braceMatch = QColor("#4a3d78");
    theme.findHighlight = QColor("#c084fc");
    theme.error = QColor("#e5534b");
    theme.warning = QColor("#d4a72c");
    theme.window = QColor("#120f19");
    theme.button = QColor("#251f36");
    theme.alternateBase = QColor("#1c1727");
    theme.highlight = QColor("#5b3fc4");
    theme.accent = QColor("#a78bfa");
    theme.accentText = QColor("#ede7ff");
    theme.mutedText = QColor("#8d84b0");
    theme.border = QColor("#3a3058");
    theme.line = QColor("#251f36");
    theme.hover = QColor("#201a2e");
    theme.syntaxStyles = {
        {"comment", italic("#6e6790")},         {"string", plain("#9fe0c0")},
        {"string.special", plain("#7dd3fc")},   {"escape", plain("#7dd3fc")},
        {"number", plain("#f7a8c4")},           {"constant", plain("#f7a8c4")},
        {"constant.builtin", plain("#f7a8c4")}, {"keyword", plain("#c4a1ff")},
        {"operator", plain("#89ddff")},         {"type", plain("#f5c2e7")},
        {"type.builtin", plain("#f5c2e7")},     {"constructor", plain("#f5c2e7")},
        {"module", plain("#f5c2e7")},           {"function", plain("#8fb4ff")},
        {"function.builtin", plain("#89ddff")}, {"variable", plain("#ddd6f3")},
        {"variable.builtin", plain("#ff8fb1")}, {"variable.parameter", plain("#f0c38e")},
        {"property", plain("#b9a8ff")},         {"label", plain("#ff8fb1")},
        {"attribute", plain("#f0c38e")},        {"tag", plain("#ff8fb1")},
        {"punctuation", plain("#a39cc2")},      {"punctuation.special", plain("#c4a1ff")},
        {"embedded", plain("#ddd6f3")},         {"heading", bold("#c4a1ff")},
        {"diff.plus", plain("#9fe0c0")},        {"diff.minus", plain("#ff8fb1")},
    };
    return theme;
}

Theme Theme::light()
{
    Theme theme;
    theme.name = QStringLiteral("light");
    theme.title = QStringLiteral("Light");
    theme.isDark = false;
    theme.font = editorFont();
    theme.background = QColor("#fafafa");
    theme.foreground = QColor("#383a42");
    theme.caret = QColor("#526fff");
    theme.caretLine = QColor("#f0f0f1");
    theme.selection = QColor("#d7dae0");
    theme.marginBackground = QColor("#fafafa");
    theme.marginForeground = QColor("#9d9d9f");
    theme.currentLineNumber = QColor("#383a42");
    theme.braceMatch = QColor("#c8ccd4");
    theme.window = QColor("#eef0f3");
    theme.button = QColor("#ffffff");
    theme.alternateBase = QColor("#f4f5f7");
    theme.highlight = QColor("#4a6cf7");
    theme.accent = QColor("#4a6cf7");
    theme.accentText = QColor("#1f2329");
    theme.mutedText = QColor("#6b7080");
    theme.border = QColor("#c8ccd4");
    theme.line = QColor("#dfe2e7");
    theme.hover = QColor("#eceef2");
    theme.findHighlight = QColor("#c18401");
    theme.error = QColor("#d1242f");
    theme.warning = QColor("#9a6700");
    theme.syntaxStyles = {
        {"comment", italic("#a0a1a7")},         {"string", plain("#50a14f")},
        {"string.special", plain("#0184bc")},   {"escape", plain("#0184bc")},
        {"number", plain("#986801")},           {"constant", plain("#986801")},
        {"constant.builtin", plain("#986801")}, {"keyword", plain("#a626a4")},
        {"operator", plain("#0184bc")},         {"type", plain("#c18401")},
        {"type.builtin", plain("#c18401")},     {"constructor", plain("#c18401")},
        {"module", plain("#c18401")},           {"function", plain("#4078f2")},
        {"function.builtin", plain("#0184bc")}, {"variable", plain("#383a42")},
        {"variable.builtin", plain("#e45649")}, {"variable.parameter", plain("#986801")},
        {"property", plain("#e45649")},         {"label", plain("#e45649")},
        {"attribute", plain("#986801")},        {"tag", plain("#e45649")},
        {"punctuation", plain("#383a42")},      {"punctuation.special", plain("#a626a4")},
        {"embedded", plain("#383a42")},         {"heading", bold("#e45649")},
        {"diff.plus", plain("#50a14f")},        {"diff.minus", plain("#e45649")},
    };
    return theme;
}

QList<Theme> Theme::all()
{
    QList<Theme> themes{duet(), dark(), purple(), light()};
    themes += VsCodeThemes::all();
    themes += KdeThemes::all();
    return themes;
}

Theme Theme::named(const QString &name)
{
    for (const Theme &theme : all()) {
        if (theme.name == name)
            return theme;
    }
    return duet();
}

int Theme::syntaxStyleIndex(QStringView captureName) const
{
    QStringView candidate = captureName;
    while (!candidate.isEmpty()) {
        for (int i = 0; i < syntaxStyles.size(); ++i) {
            if (syntaxStyles[i].first == candidate)
                return i;
        }
        const qsizetype lastDot = candidate.lastIndexOf(u'.');
        if (lastDot < 0)
            break;
        candidate = candidate.left(lastDot);
    }
    return -1;
}
