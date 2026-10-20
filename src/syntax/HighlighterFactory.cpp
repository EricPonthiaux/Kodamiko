// SPDX-License-Identifier: GPL-3.0-only

#include "HighlighterFactory.h"

#include "LexillaHighlighter.h"
#include "PlainTextHighlighter.h"
#include "TreeSitterGrammars.h"
#include "TreeSitterHighlighter.h"

#include <Qsci/qscilexerbash.h>
#include <Qsci/qscilexerbatch.h>
#include <Qsci/qscilexercmake.h>
#include <Qsci/qscilexercss.h>
#include <Qsci/qscilexerdiff.h>
#include <Qsci/qscilexerhtml.h>
#include <Qsci/qscilexerjson.h>
#include <Qsci/qscilexermakefile.h>
#include <Qsci/qscilexermarkdown.h>
#include <Qsci/qscilexerproperties.h>
#include <Qsci/qscilexerpython.h>
#include <Qsci/qscilexersql.h>
#include <Qsci/qscilexerxml.h>
#include <Qsci/qscilexeryaml.h>

namespace HighlighterFactory {
namespace {

std::unique_ptr<QsciLexer> createLexer(const QString &languageId)
{
    if (languageId == QLatin1String("json"))
        return std::make_unique<QsciLexerJSON>();
    if (languageId == QLatin1String("cmake"))
        return std::make_unique<QsciLexerCMake>();
    if (languageId == QLatin1String("markdown"))
        return std::make_unique<QsciLexerMarkdown>();
    if (languageId == QLatin1String("xml"))
        return std::make_unique<QsciLexerXML>();
    if (languageId == QLatin1String("html"))
        return std::make_unique<QsciLexerHTML>();
    if (languageId == QLatin1String("css"))
        return std::make_unique<QsciLexerCSS>();
    if (languageId == QLatin1String("bash"))
        return std::make_unique<QsciLexerBash>();
    if (languageId == QLatin1String("python"))
        return std::make_unique<QsciLexerPython>();
    if (languageId == QLatin1String("yaml"))
        return std::make_unique<QsciLexerYAML>();
    if (languageId == QLatin1String("makefile"))
        return std::make_unique<QsciLexerMakefile>();
    if (languageId == QLatin1String("properties"))
        return std::make_unique<QsciLexerProperties>();
    if (languageId == QLatin1String("diff"))
        return std::make_unique<QsciLexerDiff>();
    if (languageId == QLatin1String("batch"))
        return std::make_unique<QsciLexerBatch>();
    if (languageId == QLatin1String("sql"))
        return std::make_unique<QsciLexerSQL>();
    return nullptr;
}

} // namespace

std::unique_ptr<ISyntaxHighlighter> create(const QString &languageId)
{
    if (const TreeSitterGrammar *grammar = TreeSitterGrammars::forLanguage(languageId))
        return std::make_unique<TreeSitterHighlighter>(*grammar);
    if (std::unique_ptr<QsciLexer> lexer = createLexer(languageId))
        return std::make_unique<LexillaHighlighter>(std::move(lexer));
    return std::make_unique<PlainTextHighlighter>();
}

} // namespace HighlighterFactory
