// SPDX-License-Identifier: GPL-3.0-only

#include "VsCodeThemes.h"

#include "AppFonts.h"

// Where the colors come from, in each extension's theme file:
// - the editor's from `colors` (editor.*, editorLineNumber.*…), the
//   translucent ones flattened over the editor's background;
// - the interface's from the workbench: window ← sideBar.background,
//   alternateBase ← editorWidget.background, highlight ←
//   list.inactiveSelectionBackground, hover ← list.hoverBackground, mutedText
//   ← tab.inactiveForeground, line ← the borders between the parts;
// - each syntax category from the TextMate rule VS Code applies to the
//   matching scope (keyword ← keyword.control, type ← entity.name.type,
//   property ← variable.other.property…).
// Exceptions: "variable" is the editor's text, since tree-sitter tags every
// identifier while VS Code's grammars leave most of them unscoped; and where
// a theme defines no color for a role, or one invisible in it (borders
// darker than the panels, a find color too dark to draw translucent), the
// role takes a color of the theme's own palette.

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

// Binaryify/OneDark-Pro 3.20.3, themes/OneDark-Pro.json.
Theme oneDarkPro()
{
    Theme theme;
    theme.name = QStringLiteral("one-dark-pro");
    theme.title = QStringLiteral("One Dark Pro");
    theme.font = editorFont();
    theme.background = QColor("#282c34");
    theme.foreground = QColor("#abb2bf");
    theme.caret = QColor("#528bff");
    theme.caretLine = QColor("#2c313c");
    theme.selection = QColor("#404859");
    theme.marginBackground = QColor("#282c34");
    theme.marginForeground = QColor("#495162");
    theme.currentLineNumber = QColor("#abb2bf");
    theme.braceMatch = QColor("#515a6b");
    theme.findHighlight = QColor("#d19a66");
    theme.error = QColor("#c24038");
    theme.warning = QColor("#d19a66");
    theme.window = QColor("#21252b");
    theme.button = QColor("#30333d");
    theme.alternateBase = QColor("#21252b");
    theme.highlight = QColor("#323842");
    theme.accent = QColor("#4d78cc");
    theme.accentText = QColor("#dcdcdc");
    theme.mutedText = QColor("#7f848e");
    theme.border = QColor("#3e4452");
    theme.line = QColor("#181a1f");
    theme.hover = QColor("#2c313a");
    theme.syntaxStyles = {
        {"comment", italic("#7f848e")},         {"string", plain("#98c379")},
        {"string.special", plain("#e06c75")},   {"escape", plain("#56b6c2")},
        {"number", plain("#d19a66")},           {"constant", plain("#e5c07b")},
        {"constant.builtin", plain("#d19a66")}, {"keyword", plain("#c678dd")},
        {"operator", plain("#56b6c2")},         {"type", plain("#e5c07b")},
        {"type.builtin", plain("#c678dd")},     {"constructor", plain("#61afef")},
        {"module", plain("#e5c07b")},           {"function", plain("#61afef")},
        {"function.builtin", plain("#56b6c2")}, {"variable", plain("#abb2bf")},
        {"variable.builtin", plain("#e5c07b")}, {"variable.parameter", italic("#abb2bf")},
        {"property", plain("#e06c75")},         {"label", plain("#e06c75")},
        {"attribute", plain("#d19a66")},        {"tag", plain("#e06c75")},
        {"punctuation", plain("#abb2bf")},      {"punctuation.special", plain("#c678dd")},
        {"embedded", plain("#abb2bf")},         {"heading", bold("#e06c75")},
        {"diff.plus", plain("#98c379")},        {"diff.minus", plain("#e06c75")},
    };
    return theme;
}

// dracula/visual-studio-code 2.25.1, theme/dracula.json. Dracula marks the
// caret's line with a border only: its line is a shade of the background.
Theme dracula()
{
    Theme theme;
    theme.name = QStringLiteral("dracula");
    theme.title = QStringLiteral("Dracula");
    theme.font = editorFont();
    theme.background = QColor("#282a36");
    theme.foreground = QColor("#f8f8f2");
    theme.caret = QColor("#f8f8f2");
    theme.caretLine = QColor("#32343f");
    theme.selection = QColor("#44475a");
    theme.marginBackground = QColor("#282a36");
    theme.marginForeground = QColor("#6272a4");
    theme.currentLineNumber = QColor("#f8f8f2");
    theme.braceMatch = QColor("#44475a");
    theme.findHighlight = QColor("#ffb86c");
    theme.error = QColor("#ff5555");
    theme.warning = QColor("#8be9fd");
    theme.window = QColor("#21222c");
    theme.button = QColor("#44475a");
    theme.alternateBase = QColor("#21222c");
    theme.highlight = QColor("#44475a");
    theme.accent = QColor("#ff79c6");
    theme.accentText = QColor("#f8f8f2");
    theme.mutedText = QColor("#6272a4");
    theme.border = QColor("#52535c");
    theme.line = QColor("#191a21");
    theme.hover = QColor("#353747");
    theme.syntaxStyles = {
        {"comment", plain("#6272a4")},
        {"string", plain("#f1fa8c")},
        {"string.special", plain("#f1fa8c")},
        {"escape", plain("#ff79c6")},
        {"number", plain("#bd93f9")},
        {"constant", plain("#bd93f9")},
        {"constant.builtin", plain("#bd93f9")},
        {"keyword", plain("#ff79c6")},
        {"operator", plain("#ff79c6")},
        {"type", plain("#8be9fd")},
        {"type.builtin", plain("#ff79c6")},
        {"constructor", plain("#50fa7b")},
        {"module", plain("#f8f8f2")},
        {"function", plain("#50fa7b")},
        {"function.builtin", plain("#8be9fd")},
        {"variable", plain("#f8f8f2")},
        {"variable.builtin", italic("#bd93f9")},
        {"variable.parameter", italic("#ffb86c")},
        {"property", plain("#f8f8f2")},
        {"label", plain("#f8f8f2")},
        {"attribute", italic("#50fa7b")},
        {"tag", plain("#ff79c6")},
        {"punctuation", plain("#f8f8f2")},
        {"punctuation.special", plain("#ff79c6")},
        {"embedded", plain("#f8f8f2")},
        {"heading", bold("#bd93f9")},
        {"diff.plus", plain("#50fa7b")},
        {"diff.minus", plain("#ff5555")},
    };
    return theme;
}

// primer/github-vscode-theme 6.3.5, themes/dark-default.json. It sets no
// selection color: the one VS Code then draws.
Theme githubDark()
{
    Theme theme;
    theme.name = QStringLiteral("github-dark");
    theme.title = QStringLiteral("GitHub Dark");
    theme.font = editorFont();
    theme.background = QColor("#0d1117");
    theme.foreground = QColor("#e6edf3");
    theme.caret = QColor("#2f81f7");
    theme.caretLine = QColor("#171b22");
    theme.selection = QColor("#264f78");
    theme.marginBackground = QColor("#0d1117");
    theme.marginForeground = QColor("#6e7681");
    theme.currentLineNumber = QColor("#e6edf3");
    theme.braceMatch = QColor("#1a3b25");
    theme.findHighlight = QColor("#f2cc60");
    theme.error = QColor("#f85149");
    theme.warning = QColor("#d29922");
    theme.window = QColor("#010409");
    theme.button = QColor("#282e33");
    theme.alternateBase = QColor("#161b22");
    theme.highlight = QColor("#343941");
    theme.accent = QColor("#1f6feb");
    theme.accentText = QColor("#e6edf3");
    theme.mutedText = QColor("#7d8590");
    theme.border = QColor("#30363d");
    theme.line = QColor("#30363d");
    theme.hover = QColor("#171b22");
    theme.syntaxStyles = {
        {"comment", plain("#8b949e")},          {"string", plain("#a5d6ff")},
        {"string.special", plain("#a5d6ff")},   {"escape", plain("#ff7b72")},
        {"number", plain("#79c0ff")},           {"constant", plain("#79c0ff")},
        {"constant.builtin", plain("#79c0ff")}, {"keyword", plain("#ff7b72")},
        {"operator", plain("#ff7b72")},         {"type", plain("#ffa657")},
        {"type.builtin", plain("#ff7b72")},     {"constructor", plain("#d2a8ff")},
        {"module", plain("#ffa657")},           {"function", plain("#d2a8ff")},
        {"function.builtin", plain("#79c0ff")}, {"variable", plain("#e6edf3")},
        {"variable.builtin", plain("#79c0ff")}, {"variable.parameter", plain("#e6edf3")},
        {"property", plain("#e6edf3")},         {"label", plain("#ffa657")},
        {"attribute", plain("#79c0ff")},        {"tag", plain("#7ee787")},
        {"punctuation", plain("#e6edf3")},      {"punctuation.special", plain("#e6edf3")},
        {"embedded", plain("#e6edf3")},         {"heading", bold("#79c0ff")},
        {"diff.plus", plain("#7ee787")},        {"diff.minus", plain("#ffa198")},
    };
    return theme;
}

// enkia/tokyo-night-vscode-theme 1.1.2, themes/tokyo-night-color-theme.json.
// Its accent (#3d59a1) and find color are too dark for links and for a
// translucent match: the palette's blue instead.
Theme tokyoNight()
{
    Theme theme;
    theme.name = QStringLiteral("tokyo-night");
    theme.title = QStringLiteral("Tokyo Night");
    theme.font = editorFont();
    theme.background = QColor("#1a1b26");
    theme.foreground = QColor("#a9b1d6");
    theme.caret = QColor("#c0caf5");
    theme.caretLine = QColor("#1e202e");
    theme.selection = QColor("#2b2f41");
    theme.marginBackground = QColor("#1a1b26");
    theme.marginForeground = QColor("#363b54");
    theme.currentLineNumber = QColor("#787c99");
    theme.braceMatch = QColor("#42465d");
    theme.findHighlight = QColor("#7aa2f7");
    theme.error = QColor("#db4b4b");
    theme.warning = QColor("#e0af68");
    theme.window = QColor("#16161e");
    theme.button = QColor("#3b3e52");
    theme.alternateBase = QColor("#16161e");
    theme.highlight = QColor("#202330");
    theme.accent = QColor("#7aa2f7");
    theme.accentText = QColor("#c0caf5");
    theme.mutedText = QColor("#787c99");
    theme.border = QColor("#373949");
    theme.line = QColor("#101014");
    theme.hover = QColor("#13131a");
    theme.syntaxStyles = {
        {"comment", italic("#51597d")},         {"string", plain("#9ece6a")},
        {"string.special", plain("#b4f9f8")},   {"escape", plain("#89ddff")},
        {"number", plain("#ff9e64")},           {"constant", plain("#ff9e64")},
        {"constant.builtin", plain("#ff9e64")}, {"keyword", plain("#bb9af7")},
        {"operator", plain("#89ddff")},         {"type", plain("#0db9d7")},
        {"type.builtin", plain("#bb9af7")},     {"constructor", plain("#7aa2f7")},
        {"module", plain("#0db9d7")},           {"function", plain("#7aa2f7")},
        {"function.builtin", plain("#0db9d7")}, {"variable", plain("#c0caf5")},
        {"variable.builtin", plain("#f7768e")}, {"variable.parameter", plain("#e0af68")},
        {"property", plain("#7dcfff")},         {"label", plain("#c0caf5")},
        {"attribute", plain("#bb9af7")},        {"tag", plain("#f7768e")},
        {"punctuation", plain("#89ddff")},      {"punctuation.special", plain("#7dcfff")},
        {"embedded", plain("#a9b1d6")},         {"heading", bold("#89ddff")},
        {"diff.plus", plain("#449dab")},        {"diff.minus", plain("#914c54")},
    };
    return theme;
}

// catppuccin/vscode 3.19.0, themes/mocha.json.
Theme catppuccinMocha()
{
    Theme theme;
    theme.name = QStringLiteral("catppuccin-mocha");
    theme.title = QStringLiteral("Catppuccin Mocha");
    theme.font = editorFont();
    theme.background = QColor("#1e1e2e");
    theme.foreground = QColor("#cdd6f4");
    theme.caret = QColor("#f5e0dc");
    theme.caretLine = QColor("#2a2b3c");
    theme.selection = QColor("#3b3d4f");
    theme.marginBackground = QColor("#1e1e2e");
    theme.marginForeground = QColor("#7f849c");
    theme.currentLineNumber = QColor("#cba6f7");
    theme.braceMatch = QColor("#45475a");
    theme.findHighlight = QColor("#f38ba8");
    theme.error = QColor("#f38ba8");
    theme.warning = QColor("#fab387");
    theme.window = QColor("#181825");
    theme.button = QColor("#313244");
    theme.alternateBase = QColor("#181825");
    theme.highlight = QColor("#313244");
    theme.accent = QColor("#cba6f7");
    theme.accentText = QColor("#cba6f7");
    theme.mutedText = QColor("#6c7086");
    theme.border = QColor("#45475a");
    theme.line = QColor("#313244");
    theme.hover = QColor("#282839");
    theme.syntaxStyles = {
        {"comment", italic("#9399b2")},
        {"string", plain("#a6e3a1")},
        {"string.special", plain("#a6e3a1")},
        {"escape", plain("#f5c2e7")},
        {"number", plain("#fab387")},
        {"constant", plain("#fab387")},
        {"constant.builtin", plain("#fab387")},
        {"keyword", plain("#cba6f7")},
        {"operator", plain("#94e2d5")},
        {"type", italic("#f9e2af")},
        {"type.builtin", plain("#cba6f7")},
        {"constructor", italic("#89b4fa")},
        {"module", plain("#f9e2af")},
        {"function", italic("#89b4fa")},
        {"function.builtin", italic("#f38ba8")},
        {"variable", plain("#cdd6f4")},
        {"variable.builtin", plain("#f38ba8")},
        {"variable.parameter", italic("#eba0ac")},
        {"property", plain("#cdd6f4")},
        {"label", plain("#cdd6f4")},
        {"attribute", plain("#f9e2af")},
        {"tag", plain("#89b4fa")},
        {"punctuation", plain("#9399b2")},
        {"punctuation.special", plain("#9399b2")},
        {"embedded", plain("#cdd6f4")},
        {"heading", bold("#f38ba8")},
        {"diff.plus", plain("#a6e3a1")},
        {"diff.minus", plain("#f38ba8")},
    };
    return theme;
}

} // namespace

namespace VsCodeThemes {

QList<Theme> all()
{
    return {oneDarkPro(), dracula(), githubDark(), tokyoNight(), catppuccinMocha()};
}

} // namespace VsCodeThemes
