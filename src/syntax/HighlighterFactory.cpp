// SPDX-License-Identifier: GPL-3.0-only

#include "HighlighterFactory.h"

#include "LexillaHighlighter.h"
#include "PlainTextHighlighter.h"
#include "TreeSitterGrammars.h"
#include "TreeSitterHighlighter.h"

namespace HighlighterFactory {

std::unique_ptr<ISyntaxHighlighter> create(const QString &languageId)
{
    if (const TreeSitterGrammar *grammar = TreeSitterGrammars::forLanguage(languageId))
        return std::make_unique<TreeSitterHighlighter>(*grammar);
    if (const LexillaLanguage *language = LexillaLanguages::forLanguage(languageId))
        return std::make_unique<LexillaHighlighter>(*language);
    return std::make_unique<PlainTextHighlighter>();
}

} // namespace HighlighterFactory
