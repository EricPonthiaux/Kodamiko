// SPDX-License-Identifier: GPL-3.0-only

#include "CodeColorizer.h"

#include "EditorWidget.h"
#include "syntax/LanguageRegistry.h"

#include <QHash>

namespace {

// Scintilla colors are 0xBBGGRR.
QColor fromScintilla(long color)
{
    return {static_cast<int>(color & 0xff), static_cast<int>((color >> 8) & 0xff),
            static_cast<int>((color >> 16) & 0xff)};
}

// UTF-8 length of the character at `index`, a surrogate pair counting once.
int utf8Length(const QString &text, qsizetype index, qsizetype *characters)
{
    const char16_t unit = text.at(index).unicode();
    if (QChar::isHighSurrogate(unit) && index + 1 < text.size()) {
        *characters = 2;
        return 4;
    }
    *characters = 1;
    if (unit < 0x80)
        return 1;
    return unit < 0x800 ? 2 : 3;
}

} // namespace

// The hidden editor is styled with a probe theme: each category has its
// own color, which tells, read back, which category a style is.
CodeColorizer::CodeColorizer()
{
    Theme probe = Theme::kodamiko();
    probe.foreground = QColor(1, 2, 3);
    for (int i = 0; i < probe.syntaxStyles.size(); ++i) {
        categories_.append(probe.syntaxStyles[i].first);
        probe.syntaxStyles[i].second = {QColor(10, 20 + i / 250, i % 250), false, false};
    }
    editor_ = std::make_unique<EditorWidget>(probe);
}

CodeColorizer::~CodeColorizer() = default;

QString CodeColorizer::languageOfFence(const QString &info)
{
    const QString name = info.trimmed().section(QLatin1Char(' '), 0, 0).toLower();
    if (name.isEmpty())
        return LanguageRegistry::plainText;
    // Names that are not file extensions.
    static const QHash<QString, QString> aliases = {
        {"c++", "x.cpp"},       {"cplusplus", "x.cpp"},   {"python", "x.py"},          {"javascript", "x.js"},
        {"typescript", "x.ts"}, {"shell", "x.sh"},        {"console", "x.sh"},         {"shell-session", "x.sh"},
        {"bash", "x.sh"},       {"zsh", "x.sh"},          {"yaml", "x.yml"},           {"markdown", "x.md"},
        {"make", "Makefile"},   {"makefile", "Makefile"}, {"cmake", "CMakeLists.txt"}, {"batch", "x.bat"},
        {"dos", "x.bat"},       {"jsonc", "x.json"},      {"patch", "x.diff"},         {"toml", "x.toml"},
        {"ini", "x.ini"},
    };
    return LanguageRegistry::languageForFile(aliases.value(name, QStringLiteral("x.") + name));
}

QList<CodeColorizer::Span> CodeColorizer::categorize(const QString &code, const QString &languageId)
{
    QList<Span> spans;
    if (code.isEmpty() || languageId == LanguageRegistry::plainText)
        return spans;
    editor_->showReadOnlyText(code, languageId);
    const long length = editor_->SendScintilla(QsciScintillaBase::SCI_GETLENGTH);
    editor_->SendScintilla(QsciScintillaBase::SCI_COLOURISE, 0, -1L);

    QHash<int, QString> categoryOfStyle;
    const auto categoryOf = [this, &categoryOfStyle](int style) {
        if (const auto it = categoryOfStyle.constFind(style); it != categoryOfStyle.cend())
            return *it;
        const QColor color = fromScintilla(editor_->SendScintilla(QsciScintillaBase::SCI_STYLEGETFORE, style));
        QString category;
        if (color.red() == 10 && color.green() >= 20) {
            const int index = (color.green() - 20) * 250 + color.blue();
            category = categories_.value(index);
        }
        categoryOfStyle.insert(style, category);
        return category;
    };

    // Runs of one style, walking bytes (Scintilla) and characters (QString).
    long byte = 0;
    qsizetype index = 0;
    while (byte < length && index < code.size()) {
        const int style = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETSTYLEAT, byte));
        const qsizetype runStart = index;
        while (byte < length && index < code.size() &&
               editor_->SendScintilla(QsciScintillaBase::SCI_GETSTYLEAT, byte) == style) {
            qsizetype characters = 1;
            byte += utf8Length(code, index, &characters);
            index += characters;
        }
        if (const QString category = categoryOf(style); !category.isEmpty())
            spans.append({static_cast<int>(runStart), static_cast<int>(index - runStart), category});
    }
    return spans;
}
