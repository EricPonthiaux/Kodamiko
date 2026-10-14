#include "Theme.h"

#include <QFontDatabase>

namespace {

QFont editorFont()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    return font;
}

TextStyle plain(const char *color) { return {QColor(color)}; }
TextStyle bold(const char *color) { return {QColor(color), true, false}; }
TextStyle italic(const char *color) { return {QColor(color), false, true}; }

} // namespace

Theme Theme::dark()
{
    Theme theme;
    theme.name = QStringLiteral("dark");
    theme.font = editorFont();
    theme.background = QColor("#1f2329");
    theme.foreground = QColor("#abb2bf");
    theme.caret = QColor("#528bff");
    theme.caretLine = QColor("#2c313a");
    theme.selection = QColor("#3e4451");
    theme.marginBackground = QColor("#1f2329");
    theme.marginForeground = QColor("#5c6370");
    theme.braceMatch = QColor("#515a6b");
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

Theme Theme::light()
{
    Theme theme;
    theme.name = QStringLiteral("light");
    theme.font = editorFont();
    theme.background = QColor("#fafafa");
    theme.foreground = QColor("#383a42");
    theme.caret = QColor("#526fff");
    theme.caretLine = QColor("#f0f0f1");
    theme.selection = QColor("#d7dae0");
    theme.marginBackground = QColor("#fafafa");
    theme.marginForeground = QColor("#9d9d9f");
    theme.braceMatch = QColor("#c8ccd4");
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
