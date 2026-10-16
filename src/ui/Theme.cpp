#include "Theme.h"

#include "AppFonts.h"

namespace {

QFont editorFont()
{
    return AppFonts::codeFont(11);
}

TextStyle plain(const char *color) { return {QColor(color)}; }
TextStyle bold(const char *color) { return {QColor(color), true, false}; }
TextStyle italic(const char *color) { return {QColor(color), false, true}; }

} // namespace

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
    theme.braceMatch = QColor("#515a6b");
    theme.findHighlight = QColor("#e5c07b");
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
    theme.gradientStart = QColor("#3e6fd8");
    theme.gradientEnd = QColor("#5c8eff");
    theme.syntaxStyles = {
        {"comment", italic("#7f848e")},
        {"string", plain("#98c379")},
        {"string.special", plain("#56b6c2")},
        {"escape", plain("#56b6c2")},
        {"number", plain("#d19a66")},
        {"constant", plain("#d19a66")},
        {"constant.builtin", plain("#d19a66")},
        {"keyword", plain("#c678dd")},
        {"operator", plain("#56b6c2")},
        {"type", plain("#e5c07b")},
        {"type.builtin", plain("#e5c07b")},
        {"constructor", plain("#e5c07b")},
        {"module", plain("#e5c07b")},
        {"function", plain("#61afef")},
        {"function.builtin", plain("#56b6c2")},
        {"variable", plain("#abb2bf")},
        {"variable.builtin", plain("#e06c75")},
        {"variable.parameter", plain("#d19a66")},
        {"property", plain("#e06c75")},
        {"label", plain("#e06c75")},
        {"attribute", plain("#d19a66")},
        {"tag", plain("#e06c75")},
        {"punctuation", plain("#abb2bf")},
        {"punctuation.special", plain("#c678dd")},
        {"embedded", plain("#abb2bf")},
        {"heading", bold("#e06c75")},
        {"diff.plus", plain("#98c379")},
        {"diff.minus", plain("#e06c75")},
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
    theme.braceMatch = QColor("#4a3d78");
    theme.findHighlight = QColor("#c084fc");
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
    theme.gradientStart = QColor("#6d4fd8");
    theme.gradientEnd = QColor("#a855f7");
    theme.syntaxStyles = {
        {"comment", italic("#6e6790")},
        {"string", plain("#9fe0c0")},
        {"string.special", plain("#7dd3fc")},
        {"escape", plain("#7dd3fc")},
        {"number", plain("#f7a8c4")},
        {"constant", plain("#f7a8c4")},
        {"constant.builtin", plain("#f7a8c4")},
        {"keyword", plain("#c4a1ff")},
        {"operator", plain("#89ddff")},
        {"type", plain("#f5c2e7")},
        {"type.builtin", plain("#f5c2e7")},
        {"constructor", plain("#f5c2e7")},
        {"module", plain("#f5c2e7")},
        {"function", plain("#8fb4ff")},
        {"function.builtin", plain("#89ddff")},
        {"variable", plain("#ddd6f3")},
        {"variable.builtin", plain("#ff8fb1")},
        {"variable.parameter", plain("#f0c38e")},
        {"property", plain("#b9a8ff")},
        {"label", plain("#ff8fb1")},
        {"attribute", plain("#f0c38e")},
        {"tag", plain("#ff8fb1")},
        {"punctuation", plain("#a39cc2")},
        {"punctuation.special", plain("#c4a1ff")},
        {"embedded", plain("#ddd6f3")},
        {"heading", bold("#c4a1ff")},
        {"diff.plus", plain("#9fe0c0")},
        {"diff.minus", plain("#ff8fb1")},
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
    theme.gradientStart = QColor("#3d63f2");
    theme.gradientEnd = QColor("#6a8bff");
    theme.findHighlight = QColor("#c18401");
    theme.syntaxStyles = {
        {"comment", italic("#a0a1a7")},
        {"string", plain("#50a14f")},
        {"string.special", plain("#0184bc")},
        {"escape", plain("#0184bc")},
        {"number", plain("#986801")},
        {"constant", plain("#986801")},
        {"constant.builtin", plain("#986801")},
        {"keyword", plain("#a626a4")},
        {"operator", plain("#0184bc")},
        {"type", plain("#c18401")},
        {"type.builtin", plain("#c18401")},
        {"constructor", plain("#c18401")},
        {"module", plain("#c18401")},
        {"function", plain("#4078f2")},
        {"function.builtin", plain("#0184bc")},
        {"variable", plain("#383a42")},
        {"variable.builtin", plain("#e45649")},
        {"variable.parameter", plain("#986801")},
        {"property", plain("#e45649")},
        {"label", plain("#e45649")},
        {"attribute", plain("#986801")},
        {"tag", plain("#e45649")},
        {"punctuation", plain("#383a42")},
        {"punctuation.special", plain("#a626a4")},
        {"embedded", plain("#383a42")},
        {"heading", bold("#e45649")},
        {"diff.plus", plain("#50a14f")},
        {"diff.minus", plain("#e45649")},
    };
    return theme;
}

QList<Theme> Theme::all()
{
    return {dark(), purple(), light()};
}

Theme Theme::named(const QString &name)
{
    for (const Theme &theme : all()) {
        if (theme.name == name)
            return theme;
    }
    return dark();
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
