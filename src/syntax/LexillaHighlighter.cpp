// SPDX-License-Identifier: GPL-3.0-only

#include "LexillaHighlighter.h"

#include "ScintillaStyles.h"
#include "scintilla/ScintillaView.h"
#include "ui/Theme.h"

#include <ILexer.h>
#include <Lexilla.h>

#include <algorithm>

LexillaHighlighter::LexillaHighlighter(const LexillaLanguage &language)
    : language_(language)
{
}

void LexillaHighlighter::attach(ScintillaView *editor)
{
    editor_ = editor;
    // The document owns the lexer from here on, and releases it.
    editor_->send(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(CreateLexer(language_.lexer)));
    for (std::size_t list = 0; list < language_.keywords.size(); ++list)
        editor_->sends(SCI_SETKEYWORDS, list, language_.keywords[list]);
}

void LexillaHighlighter::applyTheme(const Theme &theme)
{
    ScintillaStyles::applyDefaultStyle(*editor_, theme);
    for (const auto &[style, category] : language_.styleCategories) {
        const int themeIndex = theme.syntaxStyleIndex(QString::fromLatin1(category));
        if (themeIndex < 0)
            continue;
        const TextStyle &textStyle = theme.syntaxStyles[themeIndex].second;
        editor_->send(SCI_STYLESETFORE, uptr_t(style), scintillaColor(textStyle.foreground));
        editor_->send(SCI_STYLESETWEIGHT, uptr_t(style), ScintillaStyles::styleWeight(theme.font, textStyle.bold));
        editor_->send(SCI_STYLESETITALIC, uptr_t(style), textStyle.italic);
    }
    editor_->recolor();
}

QString LexillaHighlighter::styleCategory(int style, const Theme &) const
{
    const auto found =
        std::ranges::find(language_.styleCategories, style, [](const auto &entry) { return entry.first; });
    return found == language_.styleCategories.end() ? QString() : QString::fromLatin1(found->second);
}
