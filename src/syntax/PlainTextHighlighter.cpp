// SPDX-License-Identifier: GPL-3.0-only

#include "PlainTextHighlighter.h"

#include "ScintillaStyles.h"
#include "scintilla/ScintillaView.h"

#include <ILexer.h>
#include <Lexilla.h>

void PlainTextHighlighter::attach(ScintillaView *editor)
{
    editor_ = editor;
    // Lexilla's "null" lexer styles everything as default text; without a
    // lexer, Scintilla would keep asking for styles (SCN_STYLENEEDED).
    editor_->send(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(CreateLexer("null")));
}

void PlainTextHighlighter::applyTheme(const Theme &theme)
{
    ScintillaStyles::applyDefaultStyle(*editor_, theme);
}

QString PlainTextHighlighter::styleCategory(int, const Theme &) const
{
    return {};
}
