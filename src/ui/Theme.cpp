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
Theme kdkNight()
{
    Theme theme;
    theme.name = QStringLiteral("kodamiko-night");
    theme.title = QStringLiteral("Kodamiko-Night");
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

// Soft violets, from a near black for the panels to a violet under the
// editor, a coral for the accent and the keywords. The code reads at 7:1 or
// about, the comments at 5.5:1; dark text over the coral, where white would
// not reach 4.5:1.
Theme kdkViolet()
{
    Theme theme;
    theme.name = QStringLiteral("kodamiko-violet");
    theme.title = QStringLiteral("Kodamiko-Violet");
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

// Over the clouds at dusk: the night blue of the sky for the panels, the
// slate of the clouds' shadows under the editor, a coral for the accent,
// the pinks and peaches of the sunset in the syntax. The code reads at 7:1
// or about, the comments at 5.5:1; dark text over the coral, where white
// would not reach 4.5:1.
Theme kdkStratos()
{
    Theme theme;
    theme.name = QStringLiteral("kodamiko-stratos");
    theme.title = QStringLiteral("Kodamiko-Stratos");
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
Theme kdkBloom()
{
    Theme theme;
    theme.name = QStringLiteral("kodamiko-bloom");
    theme.title = QStringLiteral("Kodamiko-Bloom");
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

// A warm charcoal for the editor, a shade deeper for the panels, a cream for
// the text, a clay orange for the caret and a darker one for the accent, so
// that what Claude wrote, marked in the clay orange, stands out from the
// current tab and the focus. The syntax follows the rules of the most used
// VS Code themes: one hue per role (keywords clay, types sand, functions sky,
// strings sage, constants fig), the same in every language, no pure black or
// white, italics only for the comments and the parameters. The code reads at
// 7.5:1 or more, the comments at 5.5:1, below the code but readable; the
// cream over the accent at 4.8:1.
Theme kdkClay()
{
    Theme theme;
    theme.name = QStringLiteral("kodamiko-clay");
    theme.title = QStringLiteral("Kodamiko-Clay");
    theme.font = editorFont();
    theme.background = QColor("#262624");
    theme.foreground = QColor("#e8e5da");
    theme.caret = QColor("#d97757");
    theme.caretLine = QColor("#2d2c2a");
    theme.selection = QColor("#4a3a33");
    theme.marginBackground = QColor("#262624");
    theme.marginForeground = QColor("#6b6a63");
    theme.currentLineNumber = QColor("#c2c0b6");
    theme.braceMatch = QColor("#45433e");
    theme.findHighlight = QColor("#e0b45c");
    theme.error = QColor("#ef8170");
    theme.warning = QColor("#e3a54c");
    theme.window = QColor("#1f1e1d");
    theme.button = QColor("#30302e");
    theme.alternateBase = QColor("#2b2b29");
    theme.highlight = QColor("#5a3d31");
    theme.accent = QColor("#ae5630");
    theme.accentText = QColor("#faf9f5");
    theme.mutedText = QColor("#a3a097");
    theme.border = QColor("#3e3d38");
    theme.line = QColor("#30302e");
    theme.hover = QColor("#2f2e2b");
    theme.onAccent = QColor("#faf9f5");
    theme.syntaxStyles = {
        {"comment", italic("#a29e91")},         {"string", plain("#b9cc9c")},
        {"string.special", plain("#a9d3c6")},   {"escape", plain("#a9d3c6")},
        {"number", plain("#eaabc2")},           {"constant", plain("#eaabc2")},
        {"constant.builtin", plain("#eaabc2")}, {"keyword", plain("#f2a585")},
        {"operator", plain("#a9d3c6")},         {"type", plain("#e8c992")},
        {"type.builtin", plain("#e8c992")},     {"constructor", plain("#e8c992")},
        {"module", plain("#e8c992")},           {"function", plain("#a3c6ea")},
        {"function.builtin", plain("#a9d3c6")}, {"variable", plain("#e8e5da")},
        {"variable.builtin", plain("#f4a596")}, {"variable.parameter", italic("#e8e5da")},
        {"property", plain("#d0cee0")},         {"label", plain("#f4a596")},
        {"attribute", plain("#d0cee0")},        {"tag", plain("#f4a596")},
        {"punctuation", plain("#bdbab0")},      {"punctuation.special", plain("#f2a585")},
        {"embedded", plain("#e8e5da")},         {"heading", bold("#a3c6ea")},
        {"diff.plus", plain("#b9cc9c")},        {"diff.minus", plain("#f4a596")},
    };
    return theme;
}

// Warm ivory surfaces, a coral accent, and dark syntax colors that remain
// legible on paper. The selection keeps the editor's dark foreground.
Theme kdkPearl()
{
    Theme theme;
    theme.name = QStringLiteral("kodamiko-pearl");
    theme.title = QStringLiteral("Kodamiko-Pearl");
    theme.isDark = false;
    theme.font = editorFont();
    theme.background = QColor("#fffdf9");
    theme.foreground = QColor("#383630");
    theme.caret = QColor("#b14f42");
    theme.caretLine = QColor("#f5eee6");
    theme.selection = QColor("#e8d8ca");
    theme.marginBackground = theme.background;
    theme.marginForeground = QColor("#857c72");
    theme.currentLineNumber = QColor("#60564c");
    theme.braceMatch = QColor("#dfd2c2");
    theme.findHighlight = QColor("#e1b867");
    theme.error = QColor("#a33f43");
    theme.warning = QColor("#916122");
    theme.window = QColor("#f0ece6");
    theme.button = QColor("#e7e0d7");
    theme.alternateBase = QColor("#f6f3ed");
    theme.highlight = QColor("#8d5644");
    theme.accent = QColor("#b14f42");
    theme.accentText = theme.foreground;
    theme.mutedText = QColor("#746b60");
    theme.border = QColor("#cfc7bd");
    theme.line = QColor("#e1dbd2");
    theme.hover = QColor("#e7e0d7");
    theme.onAccent = QColor("#fffdf9");
    theme.syntaxStyles = {
        {"comment", italic("#6b6258")},         {"string", plain("#3e5e3c")},
        {"string.special", plain("#365962")},   {"escape", plain("#365962")},
        {"number", plain("#784532")},           {"constant", plain("#784532")},
        {"constant.builtin", plain("#784532")}, {"keyword", plain("#88384a")},
        {"operator", plain("#54554c")},         {"type", plain("#694d2a")},
        {"type.builtin", plain("#694d2a")},     {"constructor", plain("#694d2a")},
        {"module", plain("#694d2a")},           {"function", plain("#365962")},
        {"function.builtin", plain("#365962")}, {"variable", plain("#383630")},
        {"variable.builtin", plain("#784532")}, {"variable.parameter", italic("#694b63")},
        {"property", plain("#694b63")},         {"label", plain("#784532")},
        {"attribute", plain("#694b63")},        {"tag", plain("#88384a")},
        {"punctuation", plain("#54554c")},      {"punctuation.special", plain("#88384a")},
        {"embedded", plain("#383630")},         {"heading", bold("#365962")},
        {"diff.plus", plain("#3e5e3c")},        {"diff.minus", plain("#88384a")},
    };
    return theme;
}

// Boreal light: a deep green night, mint focus and lavender/cyan syntax.
Theme kdkAurora()
{
    Theme theme;
    theme.name = QStringLiteral("kodamiko-aurora");
    theme.title = QStringLiteral("Kodamiko-Aurora");
    theme.font = editorFont();
    theme.background = QColor("#102522");
    theme.foreground = QColor("#e0f1e9");
    theme.caret = QColor("#82edc1");
    theme.caretLine = QColor("#19312d");
    theme.selection = QColor("#294a43");
    theme.marginBackground = theme.background;
    theme.marginForeground = QColor("#6e9186");
    theme.currentLineNumber = QColor("#b3decd");
    theme.braceMatch = QColor("#36574d");
    theme.findHighlight = QColor("#e9cd90");
    theme.error = QColor("#f59baf");
    theme.warning = QColor("#e9cd90");
    theme.window = QColor("#0b1b19");
    theme.button = QColor("#213b34");
    theme.alternateBase = QColor("#19312d");
    theme.highlight = QColor("#305b4e");
    theme.accent = QColor("#82edc1");
    theme.accentText = QColor("#b4f5dc");
    theme.onAccent = QColor("#0b1b19");
    theme.mutedText = QColor("#9db8ad");
    theme.border = QColor("#345148");
    theme.line = QColor("#263c35");
    theme.hover = QColor("#203b34");
    theme.syntaxStyles = {
        {"comment", italic("#94afa3")},         {"string", plain("#b7e4a0")},
        {"string.special", plain("#92ddd8")},   {"escape", plain("#92ddd8")},
        {"number", plain("#f1c995")},           {"constant", plain("#f1c995")},
        {"constant.builtin", plain("#f1c995")}, {"keyword", plain("#cbb7f5")},
        {"operator", plain("#92ddd8")},         {"type", plain("#8ee3c0")},
        {"type.builtin", plain("#8ee3c0")},     {"constructor", plain("#8ee3c0")},
        {"module", plain("#8ee3c0")},           {"function", plain("#a1cef5")},
        {"function.builtin", plain("#92ddd8")}, {"variable", plain("#e0f1e9")},
        {"variable.builtin", plain("#f2b3ce")}, {"variable.parameter", italic("#d7caee")},
        {"property", plain("#d7caee")},         {"label", plain("#f2b3ce")},
        {"attribute", plain("#d7caee")},        {"tag", plain("#f2b3ce")},
        {"punctuation", plain("#b4cdc3")},      {"punctuation.special", plain("#cbb7f5")},
        {"embedded", plain("#e0f1e9")},         {"heading", bold("#8ee3c0")},
        {"diff.plus", plain("#b7e4a0")},        {"diff.minus", plain("#f2b3ce")},
    };
    return theme;
}

// An orchid nebula: plum surfaces, rose light and champagne-colored types.
Theme kdkNebula()
{
    Theme theme;
    theme.name = QStringLiteral("kodamiko-nebula");
    theme.title = QStringLiteral("Kodamiko-Nebula");
    theme.font = editorFont();
    theme.background = QColor("#261b30");
    theme.foreground = QColor("#f0e5f4");
    theme.caret = QColor("#eda4dc");
    theme.caretLine = QColor("#32263d");
    theme.selection = QColor("#503550");
    theme.marginBackground = theme.background;
    theme.marginForeground = QColor("#95839f");
    theme.currentLineNumber = QColor("#debfdf");
    theme.braceMatch = QColor("#614562");
    theme.findHighlight = QColor("#f2d09c");
    theme.error = QColor("#ff9da9");
    theme.warning = QColor("#edc18b");
    theme.window = QColor("#191320");
    theme.button = QColor("#3b2b44");
    theme.alternateBase = QColor("#32263d");
    theme.highlight = QColor("#664165");
    theme.accent = QColor("#eda4dc");
    theme.accentText = QColor("#f7c9eb");
    theme.onAccent = QColor("#26132b");
    theme.mutedText = QColor("#b8a3c2");
    theme.border = QColor("#513b59");
    theme.line = QColor("#3b2b43");
    theme.hover = QColor("#3a2b43");
    theme.syntaxStyles = {
        {"comment", italic("#b29cbb")},         {"string", plain("#b7dfbc")},
        {"string.special", plain("#a2dcdf")},   {"escape", plain("#a2dcdf")},
        {"number", plain("#efb4c4")},           {"constant", plain("#efb4c4")},
        {"constant.builtin", plain("#efb4c4")}, {"keyword", plain("#eab0e1")},
        {"operator", plain("#a2dcdf")},         {"type", plain("#eed1a4")},
        {"type.builtin", plain("#eed1a4")},     {"constructor", plain("#eed1a4")},
        {"module", plain("#eed1a4")},           {"function", plain("#b8c4ff")},
        {"function.builtin", plain("#a2dcdf")}, {"variable", plain("#f0e5f4")},
        {"variable.builtin", plain("#ffb4b6")}, {"variable.parameter", italic("#e3c2ed")},
        {"property", plain("#d3b5f5")},         {"label", plain("#ffb4b6")},
        {"attribute", plain("#d3b5f5")},        {"tag", plain("#ffb4b6")},
        {"punctuation", plain("#ccb9d4")},      {"punctuation.special", plain("#eab0e1")},
        {"embedded", plain("#f0e5f4")},         {"heading", bold("#eab0e1")},
        {"diff.plus", plain("#b7dfbc")},        {"diff.minus", plain("#ffb4b6")},
    };
    return theme;
}

// Neural signals in a midnight-blue space: ice cyan, periwinkle and lilac.
Theme kdkSynapse()
{
    Theme theme;
    theme.name = QStringLiteral("kodamiko-synapse");
    theme.title = QStringLiteral("Kodamiko-Synapse");
    theme.font = editorFont();
    theme.background = QColor("#101d32");
    theme.foreground = QColor("#e1ebfa");
    theme.caret = QColor("#79ddf2");
    theme.caretLine = QColor("#192a43");
    theme.selection = QColor("#29435c");
    theme.marginBackground = theme.background;
    theme.marginForeground = QColor("#7b91b1");
    theme.currentLineNumber = QColor("#b6d7f2");
    theme.braceMatch = QColor("#385873");
    theme.findHighlight = QColor("#f2d394");
    theme.error = QColor("#ff9fb6");
    theme.warning = QColor("#f0c58b");
    theme.window = QColor("#0a1324");
    theme.button = QColor("#20334e");
    theme.alternateBase = QColor("#192a43");
    theme.highlight = QColor("#305475");
    theme.accent = QColor("#79ddf2");
    theme.accentText = QColor("#b0edfa");
    theme.onAccent = QColor("#0a1324");
    theme.mutedText = QColor("#9aafcc");
    theme.border = QColor("#344b6b");
    theme.line = QColor("#243550");
    theme.hover = QColor("#203550");
    theme.syntaxStyles = {
        {"comment", italic("#95a7c1")},         {"string", plain("#a7e5cf")},
        {"string.special", plain("#8edfeb")},   {"escape", plain("#8edfeb")},
        {"number", plain("#f2c59d")},           {"constant", plain("#f2c59d")},
        {"constant.builtin", plain("#f2c59d")}, {"keyword", plain("#c4b7ff")},
        {"operator", plain("#8edfeb")},         {"type", plain("#96d5f5")},
        {"type.builtin", plain("#96d5f5")},     {"constructor", plain("#96d5f5")},
        {"module", plain("#96d5f5")},           {"function", plain("#a6c1ff")},
        {"function.builtin", plain("#8edfeb")}, {"variable", plain("#e1ebfa")},
        {"variable.builtin", plain("#f4b0d5")}, {"variable.parameter", italic("#d4c4f1")},
        {"property", plain("#b7cdec")},         {"label", plain("#f4b0d5")},
        {"attribute", plain("#b7cdec")},        {"tag", plain("#f4b0d5")},
        {"punctuation", plain("#b4c5df")},      {"punctuation.special", plain("#c4b7ff")},
        {"embedded", plain("#e1ebfa")},         {"heading", bold("#8edfeb")},
        {"diff.plus", plain("#a7e5cf")},        {"diff.minus", plain("#f4b0d5")},
    };
    return theme;
}

// Opalescent daylight: cool pearl and lavender surfaces, amethyst focus,
// and deep teal, blue and mulberry inks rather than pastel-colored text.
Theme kdkOpal()
{
    Theme theme;
    theme.name = QStringLiteral("kodamiko-opal");
    theme.title = QStringLiteral("Kodamiko-Opal");
    theme.isDark = false;
    theme.font = editorFont();
    theme.background = QColor("#faf8ff");
    theme.foreground = QColor("#343049");
    theme.caret = QColor("#7150a1");
    theme.caretLine = QColor("#efebf7");
    theme.selection = QColor("#dfd6ef");
    theme.marginBackground = theme.background;
    theme.marginForeground = QColor("#837792");
    theme.currentLineNumber = QColor("#66517e");
    theme.braceMatch = QColor("#d4c8e7");
    theme.findHighlight = QColor("#e4c98f");
    theme.error = QColor("#9c3956");
    theme.warning = QColor("#815b21");
    theme.window = QColor("#eee9f5");
    theme.button = QColor("#e3dcec");
    theme.alternateBase = QColor("#f3eff9");
    theme.highlight = QColor("#685084");
    theme.accent = QColor("#7150a1");
    theme.accentText = QColor("#594078");
    theme.onAccent = QColor("#faf8ff");
    theme.mutedText = QColor("#71627f");
    theme.border = QColor("#c6b9d7");
    theme.line = QColor("#dcd3e7");
    theme.hover = QColor("#e7e0f0");
    theme.syntaxStyles = {
        {"comment", italic("#6e5f7b")},         {"string", plain("#315e52")},
        {"string.special", plain("#285969")},   {"escape", plain("#285969")},
        {"number", plain("#7a4930")},           {"constant", plain("#7a4930")},
        {"constant.builtin", plain("#7a4930")}, {"keyword", plain("#69418b")},
        {"operator", plain("#285969")},         {"type", plain("#355278")},
        {"type.builtin", plain("#355278")},     {"constructor", plain("#355278")},
        {"module", plain("#355278")},           {"function", plain("#34518f")},
        {"function.builtin", plain("#285969")}, {"variable", plain("#343049")},
        {"variable.builtin", plain("#803d60")}, {"variable.parameter", italic("#6b4976")},
        {"property", plain("#6b4976")},         {"label", plain("#803d60")},
        {"attribute", plain("#6b4976")},        {"tag", plain("#803d60")},
        {"punctuation", plain("#585068")},      {"punctuation.special", plain("#69418b")},
        {"embedded", plain("#343049")},         {"heading", bold("#69418b")},
        {"diff.plus", plain("#315e52")},        {"diff.minus", plain("#803d60")},
    };
    return theme;
}

} // namespace

Theme Theme::kodamiko()
{
    Theme theme;
    theme.name = QStringLiteral("kodamiko");
    theme.title = QStringLiteral("Kodamiko");
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
    QList<Theme> themes{kodamiko(), kdkNight(),  kdkViolet(), kdkStratos(), kdkBloom(), kdkClay(),
                        kdkPearl(), kdkAurora(), kdkNebula(), kdkSynapse(), kdkOpal()};
    themes += MaterialThemes::all();
    return themes;
}

Theme Theme::named(const QString &name)
{
    for (const Theme &theme : all()) {
        if (theme.name == name)
            return theme;
    }
    return kodamiko();
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
