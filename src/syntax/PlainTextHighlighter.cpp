#include "PlainTextHighlighter.h"

#include "ScintillaStyles.h"

#include <Qsci/qsciscintilla.h>

void PlainTextHighlighter::attach(QsciScintilla *editor)
{
    editor_ = editor;
    editor_->setLexer(nullptr);
}

void PlainTextHighlighter::applyTheme(const Theme &theme)
{
    ScintillaStyles::applyDefaultStyle(*editor_, theme);
}
