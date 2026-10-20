// SPDX-License-Identifier: GPL-3.0-only

#include "KdeThemes.h"

#include "AppFonts.h"
#include "ThemeStyle.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <array>

namespace {

// The syntax categories (tree-sitter captures, see Theme) and the
// KSyntaxHighlighting style each one takes, as Kate colors the same
// constructs.
constexpr std::array<std::pair<const char *, const char *>, 28> styleOfCapture{{
    {"comment", "Comment"},
    {"string", "String"},
    {"string.special", "SpecialString"},
    {"escape", "SpecialChar"},
    {"number", "DecVal"},
    {"constant", "Constant"},
    {"constant.builtin", "Constant"},
    {"keyword", "Keyword"},
    {"operator", "Operator"},
    {"type", "DataType"},
    {"type.builtin", "DataType"},
    {"constructor", "DataType"},
    {"module", "Import"},
    {"function", "Function"},
    {"function.builtin", "BuiltIn"},
    {"variable", "Normal"},
    {"variable.builtin", "BuiltIn"},
    {"variable.parameter", "Variable"},
    {"property", "Variable"},
    {"label", "Annotation"},
    {"attribute", "Attribute"},
    {"tag", "Keyword"},
    {"punctuation", "Normal"},
    {"punctuation.special", "Operator"},
    {"embedded", "Normal"},
    {"heading", "Keyword"},
    {"diff.plus", "String"},
    {"diff.minus", "Error"},
}};

// The styles whose color can stand for the accent, in order of preference:
// the most saturated is taken.
constexpr std::array accentCandidates{"Function", "ControlFlow", "DataType", "Keyword", "Import"};

QColor colorOf(const QJsonObject &object, const char *key, const QColor &fallback)
{
    const QColor color(object.value(QLatin1String(key)).toString());
    return color.isValid() ? color : fallback;
}

TextStyle textStyle(const QJsonObject &styles, const char *name, const QColor &normal)
{
    const QJsonObject style = styles.value(QLatin1String(name)).toObject();
    return {colorOf(style, "text-color", normal), style.value(QLatin1String("bold")).toBool(),
            style.value(QLatin1String("italic")).toBool()};
}

// The most saturated of the candidate styles' colors; the selection's when
// none is colorful.
QColor accentOf(const QJsonObject &styles, const QColor &normal, const QColor &selection)
{
    QColor best = selection;
    float bestSaturation = 0.25F; // grays and pastels do not count
    for (const char *name : accentCandidates) {
        const QColor color = textStyle(styles, name, normal).foreground;
        if (color.hsvSaturationF() > bestSaturation && color.valueF() > 0.35F) {
            best = color;
            bestSaturation = color.hsvSaturationF();
        }
    }
    return best;
}

void setEditorColors(Theme &theme, const QJsonObject &editor, const QJsonObject &styles)
{
    theme.background = colorOf(editor, "BackgroundColor", theme.isDark ? QColor("#232629") : QColor("#ffffff"));
    theme.foreground = textStyle(styles, "Normal", theme.isDark ? QColor("#cfcfc2") : QColor("#1f1c1b")).foreground;
    theme.caret = theme.foreground;
    theme.caretLine = colorOf(editor, "CurrentLine", theme.background);
    theme.selection = colorOf(editor, "TextSelection", ThemeStyle::mixed(theme.background, theme.foreground, 0.2));
    theme.marginBackground = colorOf(editor, "IconBorder", theme.background);
    theme.marginForeground =
        colorOf(editor, "LineNumbers", ThemeStyle::mixed(theme.background, theme.foreground, 0.45));
    theme.currentLineNumber = colorOf(editor, "CurrentLineNumber", theme.foreground);
    theme.braceMatch = colorOf(editor, "BracketMatching", theme.selection);
    // A search's matches are drawn translucent: a vivid color, the
    // Warning style's, rather than Kate's SearchHighlight background.
    theme.findHighlight = textStyle(styles, "Warning", QColor("#e6c384")).foreground;
    theme.error = colorOf(editor, "MarkError", QColor("#e5534b"));
    theme.warning = colorOf(editor, "MarkWarning", QColor("#d4a72c"));
}

// The interface around the editor, in steps from its background toward its
// text: the same on dark and light themes.
void setInterfaceColors(Theme &theme, const QJsonObject &styles)
{
    const QColor &background = theme.background;
    const QColor &text = theme.foreground;
    theme.window = theme.isDark ? background.darker(115) : ThemeStyle::mixed(background, text, 0.06);
    theme.alternateBase = ThemeStyle::mixed(background, text, 0.04);
    theme.button = ThemeStyle::mixed(background, text, 0.09);
    theme.hover = ThemeStyle::mixed(background, text, 0.05);
    theme.line = ThemeStyle::mixed(background, text, 0.10);
    theme.border = ThemeStyle::mixed(background, text, 0.20);
    theme.mutedText = ThemeStyle::mixed(text, background, 0.4);
    theme.highlight = theme.selection;
    theme.accent = accentOf(styles, text, theme.selection);
    theme.accentText = theme.isDark ? text.lighter(115) : text;
}

void setSyntaxStyles(Theme &theme, const QJsonObject &styles)
{
    theme.syntaxStyles.clear();
    for (const auto &[capture, style] : styleOfCapture)
        theme.syntaxStyles.append({QString::fromLatin1(capture), textStyle(styles, style, theme.foreground)});
}

QList<Theme> readEmbedded()
{
    QList<Theme> themes;
    for (QDirIterator it(QStringLiteral(":/themes/kde"), {QStringLiteral("*.theme")}); it.hasNext();) {
        QFile file(it.next());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString name = QStringLiteral("kde-") + QFileInfo(file.fileName()).completeBaseName();
        if (std::optional<Theme> theme = KdeThemes::fromJson(file.readAll(), name))
            themes.append(std::move(*theme));
    }
    std::ranges::sort(themes,
                      [](const Theme &a, const Theme &b) { return QString::localeAwareCompare(a.title, b.title) < 0; });
    return themes;
}

} // namespace

namespace KdeThemes {

const QList<Theme> &all()
{
    static const QList<Theme> themes = readEmbedded();
    return themes;
}

std::optional<Theme> fromJson(const QByteArray &json, const QString &name)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QJsonObject editor = root.value(QLatin1String("editor-colors")).toObject();
    const QJsonObject styles = root.value(QLatin1String("text-styles")).toObject();
    if (editor.isEmpty() || styles.isEmpty())
        return std::nullopt;
    Theme theme;
    theme.name = name;
    theme.title = root.value(QLatin1String("metadata")).toObject().value(QLatin1String("name")).toString(name);
    theme.font = AppFonts::codeFont(11);
    theme.isDark = QColor(editor.value(QLatin1String("BackgroundColor")).toString()).lightness() < 128;
    setEditorColors(theme, editor, styles);
    setInterfaceColors(theme, styles);
    setSyntaxStyles(theme, styles);
    return theme;
}

} // namespace KdeThemes
