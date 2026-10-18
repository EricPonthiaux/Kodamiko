#include "TreeSitterGrammars.h"

#include "HighlightQuery.h"

#include <QFile>
#include <QStringList>
#include <QtDebug>

#include <map>
#include <memory>

extern "C" {
const TSLanguage *tree_sitter_c();
const TSLanguage *tree_sitter_cpp();
const TSLanguage *tree_sitter_java();
const TSLanguage *tree_sitter_javascript();
const TSLanguage *tree_sitter_typescript();
const TSLanguage *tree_sitter_tsx();
}

namespace TreeSitterGrammars {
namespace {

struct GrammarDefinition {
    const TSLanguage *(*language)();
    // Concatenated in this order. When several patterns capture the same
    // node, the last one wins, so we go from most general to most specific
    // (C before C++, JavaScript before TypeScript).
    QStringList queryResources;
    QString outlineResource;
};

const std::map<QString, GrammarDefinition> &definitions()
{
    static const std::map<QString, GrammarDefinition> table = {
        {"c", {tree_sitter_c, {":/queries/c/highlights.scm"}, ":/queries/c/outline.scm"}},
        {"cpp",
         {tree_sitter_cpp,
          {":/queries/c/highlights.scm", ":/queries/cpp/highlights.scm"},
          ":/queries/cpp/outline.scm"}},
        {"java", {tree_sitter_java, {":/queries/java/highlights.scm"}, ":/queries/java/outline.scm"}},
        {"javascript",
         {tree_sitter_javascript,
          {":/queries/javascript/highlights.scm", ":/queries/javascript/highlights-jsx.scm",
           ":/queries/javascript/highlights-params.scm"},
          ":/queries/javascript/outline.scm"}},
        {"typescript",
         {tree_sitter_typescript,
          {":/queries/javascript/highlights.scm", ":/queries/typescript/highlights.scm"},
          ":/queries/typescript/outline.scm"}},
        {"tsx",
         {tree_sitter_tsx,
          {":/queries/javascript/highlights.scm", ":/queries/javascript/highlights-jsx.scm",
           ":/queries/typescript/highlights.scm"},
          ":/queries/typescript/outline.scm"}},
    };
    return table;
}

QByteArray readQuerySources(const QStringList &resources)
{
    QByteArray source;
    for (const QString &resource : resources) {
        QFile file(resource);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "Tree-sitter query not found:" << resource;
            continue;
        }
        source += file.readAll();
        source += '\n';
    }
    return source;
}

struct LoadedGrammar {
    std::unique_ptr<HighlightQuery> query;
    std::unique_ptr<HighlightQuery> outline;    // nullptr if the query is invalid
    std::unique_ptr<TreeSitterGrammar> grammar; // nullptr if the query is invalid
};

} // namespace

const TreeSitterGrammar *forLanguage(const QString &languageId)
{
    static std::map<QString, LoadedGrammar> loaded;

    if (auto it = loaded.find(languageId); it != loaded.end())
        return it->second.grammar.get();

    const auto definition = definitions().find(languageId);
    if (definition == definitions().end())
        return nullptr;

    const TSLanguage *language = definition->second.language();
    LoadedGrammar entry;
    entry.query = std::make_unique<HighlightQuery>(language, readQuerySources(definition->second.queryResources));
    entry.outline = std::make_unique<HighlightQuery>(language, readQuerySources({definition->second.outlineResource}));
    if (!entry.outline->isValid()) {
        qWarning().noquote() << "Invalid outline query" << languageId << ":" << entry.outline->errorMessage();
        entry.outline.reset();
    }
    if (entry.query->isValid())
        entry.grammar =
            std::make_unique<TreeSitterGrammar>(TreeSitterGrammar{language, *entry.query, entry.outline.get()});
    else
        qWarning().noquote() << "Invalid highlighting query" << languageId << ":" << entry.query->errorMessage();

    const TreeSitterGrammar *grammar = entry.grammar.get();
    loaded.emplace(languageId, std::move(entry));
    return grammar;
}

} // namespace TreeSitterGrammars
