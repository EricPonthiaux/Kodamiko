// SPDX-License-Identifier: GPL-3.0-only

#include "Theme.h"

#include "AppFonts.h"
#include "MaterialThemes.h"

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

QList<Theme> Theme::all()
{
    QList<Theme> themes{duet()};
    themes += MaterialThemes::all();
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
