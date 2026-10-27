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

// The editor of the website's demo (site/assets/style.css, "--demo-"): near
// black, the hairlines of white at a few percent drawn here opaque, and a
// sober syntax where the keywords take the accent's light shade, the types
// gold and the functions the brightest text.
Theme duetNight()
{
    Theme theme;
    theme.name = QStringLiteral("duet-night");
    theme.title = QStringLiteral("Duet-Night");
    theme.font = editorFont();
    theme.background = QColor("#0e1015");
    theme.foreground = QColor("#c9cdd6");
    theme.caret = QColor("#eceef3");
    theme.caretLine = QColor("#14161b");
    theme.selection = QColor("#1e2b4c");
    theme.marginBackground = QColor("#0e1015");
    theme.marginForeground = QColor("#565c6a");
    theme.currentLineNumber = QColor("#9ba2b1");
    theme.braceMatch = QColor("#2a2e38");
    theme.findHighlight = QColor("#d7b46a");
    theme.error = QColor("#f07178");
    theme.warning = QColor("#d7a34a");
    theme.window = QColor("#0b0d11");
    theme.button = QColor("#1f2228");
    theme.alternateBase = QColor("#13151a");
    theme.highlight = QColor("#2f4682");
    theme.accent = QColor("#4f7cf0");
    theme.accentText = QColor("#ffffff");
    // White over the blue accent reaches only 3.8:1.
    theme.onAccent = QColor("#0b0d11");
    theme.mutedText = QColor("#8b92a2");
    theme.border = QColor("#2a2d35");
    theme.line = QColor("#1f2125");
    theme.hover = QColor("#1a1c21");
    theme.syntaxStyles = {
        {"comment", italic("#848b99")},         {"string", plain("#8fcfa3")},
        {"string.special", plain("#84a3f7")},   {"escape", plain("#84a3f7")},
        {"number", plain("#e5a37a")},           {"constant", plain("#e5a37a")},
        {"constant.builtin", plain("#e5a37a")}, {"keyword", plain("#84a3f7")},
        {"operator", plain("#9ba2b1")},         {"type", plain("#d7b46a")},
        {"type.builtin", plain("#d7b46a")},     {"constructor", plain("#d7b46a")},
        {"module", plain("#d7b46a")},           {"function", plain("#eceef3")},
        {"function.builtin", plain("#eceef3")}, {"variable", plain("#c9cdd6")},
        {"variable.builtin", plain("#f1787f")}, {"variable.parameter", italic("#c9cdd6")},
        {"property", plain("#a9b7d9")},         {"label", plain("#f1787f")},
        {"attribute", plain("#d7b46a")},        {"tag", plain("#f1787f")},
        {"punctuation", plain("#989eac")},      {"punctuation.special", plain("#84a3f7")},
        {"embedded", plain("#c9cdd6")},         {"heading", bold("#84a3f7")},
        {"diff.plus", plain("#7ed39b")},        {"diff.minus", plain("#f1787f")},
    };
    return theme;
}

// After a website: its soft violets, from the near black of its
// header for the panels to the violet of its pages under the editor, its
// coral for the accent and the keywords. The code reads at 7:1 or about,
// the comments at 5.5:1; dark text over the coral, where white would not
// reach 4.5:1.
Theme duetViolet()
{
    Theme theme;
    theme.name = QStringLiteral("duet-violet");
    theme.title = QStringLiteral("Duet-Violet");
    theme.font = editorFont();
    theme.background = QColor("#2b2045");
    theme.foreground = QColor("#e6e0f0");
    theme.caret = QColor("#f84a5e");
    theme.caretLine = QColor("#352a52");
    theme.selection = QColor("#47367a");
    theme.marginBackground = QColor("#2b2045");
    theme.marginForeground = QColor("#7d70a0");
    theme.currentLineNumber = QColor("#d6cce8");
    theme.braceMatch = QColor("#4a3b6e");
    theme.findHighlight = QColor("#f5c062");
    theme.error = QColor("#ff6b6b");
    theme.warning = QColor("#f0b45a");
    theme.window = QColor("#1f1729");
    theme.button = QColor("#3a2e58");
    theme.alternateBase = QColor("#352a52");
    theme.highlight = QColor("#5a3f96");
    theme.accent = QColor("#f84a5e");
    theme.accentText = QColor("#f5f0fa");
    theme.mutedText = QColor("#a59cb8");
    theme.border = QColor("#4a3b6e");
    theme.line = QColor("#2e2443");
    theme.hover = QColor("#352a52");
    theme.onAccent = QColor("#1a1224");
    theme.syntaxStyles = {
        {"comment", italic("#a397c0")},         {"string", plain("#b5e08c")},
        {"string.special", plain("#7fd6e0")},   {"escape", plain("#7fd6e0")},
        {"number", plain("#ffad75")},           {"constant", plain("#ffad75")},
        {"constant.builtin", plain("#ffad75")}, {"keyword", plain("#ff8fa0")},
        {"operator", plain("#7fd6e0")},         {"type", plain("#d59cff")},
        {"type.builtin", plain("#d59cff")},     {"constructor", plain("#d59cff")},
        {"module", plain("#d59cff")},           {"function", plain("#8cc8ff")},
        {"function.builtin", plain("#7fd6e0")}, {"variable", plain("#e6e0f0")},
        {"variable.builtin", plain("#ffd479")}, {"variable.parameter", italic("#f5b8d6")},
        {"property", plain("#c2b8ff")},         {"label", plain("#ffd479")},
        {"attribute", plain("#c2b8ff")},        {"tag", plain("#ffd479")},
        {"punctuation", plain("#b8aed0")},      {"punctuation.special", plain("#ff8fa0")},
        {"embedded", plain("#e6e0f0")},         {"heading", bold("#8cc8ff")},
        {"diff.plus", plain("#b5e08c")},        {"diff.minus", plain("#ff8fa0")},
    };
    return theme;
}

// After a page over the clouds at dusk: the night blue of its sky
// for the panels, the slate of the clouds' shadows under the editor, the
// coral of its button for the accent, the pinks and peaches of the lasers
// and the sunset in the syntax. The code reads at 7:1 or about, the comments
// at 5.5:1; dark text over the coral, where white would not reach 4.5:1.
Theme duetStratos()
{
    Theme theme;
    theme.name = QStringLiteral("duet-stratos");
    theme.title = QStringLiteral("Duet-Stratos");
    theme.font = editorFont();
    theme.background = QColor("#2a2935");
    theme.foreground = QColor("#ece4e6");
    theme.caret = QColor("#f85366");
    theme.caretLine = QColor("#33313e");
    theme.selection = QColor("#4e3f52");
    theme.marginBackground = QColor("#2a2935");
    theme.marginForeground = QColor("#6f6670");
    theme.currentLineNumber = QColor("#d8ccd0");
    theme.braceMatch = QColor("#4a4652");
    theme.findHighlight = QColor("#f2c48a");
    theme.error = QColor("#ff6b7a");
    theme.warning = QColor("#f0b45a");
    theme.window = QColor("#11131e");
    theme.button = QColor("#3a3846");
    theme.alternateBase = QColor("#33313e");
    theme.highlight = QColor("#6a3f55");
    theme.accent = QColor("#f85366");
    theme.accentText = QColor("#f3eef0");
    theme.mutedText = QColor("#a89aa0");
    theme.border = QColor("#4a4652");
    theme.line = QColor("#2e2c38");
    theme.hover = QColor("#33313e");
    theme.onAccent = QColor("#11131e");
    theme.syntaxStyles = {
        {"comment", italic("#a89ea9")},         {"string", plain("#b8dca0")},
        {"string.special", plain("#8fd3d8")},   {"escape", plain("#8fd3d8")},
        {"number", plain("#ffb899")},           {"constant", plain("#ffb899")},
        {"constant.builtin", plain("#ffb899")}, {"keyword", plain("#ff99ab")},
        {"operator", plain("#8fd3d8")},         {"type", plain("#e6c9a8")},
        {"type.builtin", plain("#e6c9a8")},     {"constructor", plain("#e6c9a8")},
        {"module", plain("#e6c9a8")},           {"function", plain("#a9c4f5")},
        {"function.builtin", plain("#8fd3d8")}, {"variable", plain("#ece4e6")},
        {"variable.builtin", plain("#ffd28a")}, {"variable.parameter", italic("#f5b8d6")},
        {"property", plain("#c8b8f0")},         {"label", plain("#ffd28a")},
        {"attribute", plain("#c8b8f0")},        {"tag", plain("#ffd28a")},
        {"punctuation", plain("#beb2bb")},      {"punctuation.special", plain("#ff99ab")},
        {"embedded", plain("#ece4e6")},         {"heading", bold("#a9c4f5")},
        {"diff.plus", plain("#b8dca0")},        {"diff.minus", plain("#ff99ab")},
    };
    return theme;
}

// After a garden of flowers glowing in the night: the night sky for the
// panels, the blue of the dark water under the editor, the magenta of the
// flower at its heart for the accent, the lupins, the crystals, the berries,
// the poppies and the fireflies in the syntax. The code reads at 7:1 or
// about, the comments at 5.5:1; dark text over the magenta, where white
// would not reach 4.5:1.
Theme duetBloom()
{
    Theme theme;
    theme.name = QStringLiteral("duet-bloom");
    theme.title = QStringLiteral("Duet-Bloom");
    theme.font = editorFont();
    theme.background = QColor("#172036");
    theme.foreground = QColor("#dfe4f0");
    theme.caret = QColor("#e366d0");
    theme.caretLine = QColor("#1f2a44");
    theme.selection = QColor("#33335e");
    theme.marginBackground = QColor("#172036");
    theme.marginForeground = QColor("#4e5a78");
    theme.currentLineNumber = QColor("#c4cbe0");
    theme.braceMatch = QColor("#33415e");
    theme.findHighlight = QColor("#f5c451");
    theme.error = QColor("#ff6b7a");
    theme.warning = QColor("#f0b45a");
    theme.window = QColor("#0b1428");
    theme.button = QColor("#24314a");
    theme.alternateBase = QColor("#1f2a44");
    theme.highlight = QColor("#5a2f6e");
    theme.accent = QColor("#e366d0");
    theme.accentText = QColor("#f0f2f8");
    theme.mutedText = QColor("#8f9ab4");
    theme.border = QColor("#33415e");
    theme.line = QColor("#1b2540");
    theme.hover = QColor("#1f2a44");
    theme.onAccent = QColor("#0b1428");
    theme.syntaxStyles = {
        {"comment", italic("#8d97b0")},         {"string", plain("#b3dc9c")},
        {"string.special", plain("#8fd8e8")},   {"escape", plain("#8fd8e8")},
        {"number", plain("#ffc56b")},           {"constant", plain("#ffc56b")},
        {"constant.builtin", plain("#ffc56b")}, {"keyword", plain("#c9a0ff")},
        {"operator", plain("#8fd8e8")},         {"type", plain("#f2a8d4")},
        {"type.builtin", plain("#f2a8d4")},     {"constructor", plain("#f2a8d4")},
        {"module", plain("#f2a8d4")},           {"function", plain("#8fb8ff")},
        {"function.builtin", plain("#8fd8e8")}, {"variable", plain("#dfe4f0")},
        {"variable.builtin", plain("#ff8a7a")}, {"variable.parameter", italic("#f0b8e8")},
        {"property", plain("#b8c4ff")},         {"label", plain("#ff8a7a")},
        {"attribute", plain("#b8c4ff")},        {"tag", plain("#ff8a7a")},
        {"punctuation", plain("#aab3c8")},      {"punctuation.special", plain("#c9a0ff")},
        {"embedded", plain("#dfe4f0")},         {"heading", bold("#8fb8ff")},
        {"diff.plus", plain("#b3dc9c")},        {"diff.minus", plain("#ff8a7a")},
    };
    return theme;
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
    // White over the blue accent reaches only 3.8:1.
    theme.onAccent = QColor("#0b0d11");
    theme.mutedText = QColor("#808999");
    theme.border = QColor("#353b49");
    theme.line = QColor("#2a2f3a");
    theme.hover = QColor("#262b35");
    // Flat: the same color at both ends, a shade darker than the accent for
    // white text.
    theme.syntaxStyles = {
        {"comment", italic("#9299aa")},         {"string", plain("#a8d67e")},
        {"string.special", plain("#5fd4e8")},   {"escape", plain("#5fd4e8")},
        {"number", plain("#ff9e64")},           {"constant", plain("#ff9e64")},
        {"constant.builtin", plain("#ff9e64")}, {"keyword", plain("#b3a3ff")},
        {"operator", plain("#7fb5e0")},         {"type", plain("#4fd1c5")},
        {"type.builtin", plain("#4fd1c5")},     {"constructor", plain("#4fd1c5")},
        {"module", plain("#4fd1c5")},           {"function", plain("#6cb6ff")},
        {"function.builtin", plain("#5fd4e8")}, {"variable", plain("#c9cfdb")},
        {"variable.builtin", plain("#ff8ca1")}, {"variable.parameter", italic("#f0a6ca")},
        {"property", plain("#e6c384")},         {"label", plain("#ff8ca1")},
        {"attribute", plain("#e6c384")},        {"tag", plain("#ff8ca1")},
        {"punctuation", plain("#a7aebc")},      {"punctuation.special", plain("#b3a3ff")},
        {"embedded", plain("#c9cfdb")},         {"heading", bold("#6cb6ff")},
        {"diff.plus", plain("#8fd18a")},        {"diff.minus", plain("#ff8ca1")},
    };
    return theme;
}

QList<Theme> Theme::all()
{
    QList<Theme> themes{duet(), duetNight(), duetViolet(), duetStratos(), duetBloom()};
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
