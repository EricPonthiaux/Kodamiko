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
    // Concaténées dans cet ordre. Quand plusieurs motifs capturent le même
    // nœud, le dernier l'emporte : on va donc du plus général au plus précis
    // (C avant C++, JavaScript avant TypeScript).
    QStringList queryResources;
    QString outlineResource;
};

const std::map<QString, GrammarDefinition> &definitions()
{
    static const std::map<QString, GrammarDefinition> table = {
        {"c", {tree_sitter_c, {":/queries/c/highlights.scm"}, ":/queries/c/outline.scm"}},
        {"cpp",
         {tree_sitter_cpp, {":/queries/c/highlights.scm", ":/queries/cpp/highlights.scm"},
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
            qWarning() << "Requête tree-sitter introuvable :" << resource;
            continue;
        }
        source += file.readAll();
        source += '\n';
    }
    return source;
}

struct LoadedGrammar {
    std::unique_ptr<HighlightQuery> query;
    std::unique_ptr<HighlightQuery> outline; // nullptr si la requête est invalide
    std::unique_ptr<TreeSitterGrammar> grammar; // nullptr si la requête est invalide
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
        qWarning().noquote() << "Requête de structure" << languageId << "invalide :" << entry.outline->errorMessage();
        entry.outline.reset();
    }
    if (entry.query->isValid())
        entry.grammar = std::make_unique<TreeSitterGrammar>(TreeSitterGrammar{language, *entry.query, entry.outline.get()});
    else
        qWarning().noquote() << "Requête de coloration" << languageId << "invalide :" << entry.query->errorMessage();

    const TreeSitterGrammar *grammar = entry.grammar.get();
    loaded.emplace(languageId, std::move(entry));
    return grammar;
}

} // namespace TreeSitterGrammars
