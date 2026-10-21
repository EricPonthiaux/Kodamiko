// SPDX-License-Identifier: GPL-3.0-only

#include "OccurrenceGrammars.h"

#include "syntax/HighlightQuery.h"

#include <QFile>
#include <QStringList>
#include <QtDebug>

#include <map>
#include <memory>
#include <mutex>

extern "C" {
const TSLanguage *tree_sitter_c();
const TSLanguage *tree_sitter_cpp();
const TSLanguage *tree_sitter_java();
const TSLanguage *tree_sitter_javascript();
const TSLanguage *tree_sitter_typescript();
const TSLanguage *tree_sitter_tsx();
const TSLanguage *tree_sitter_python();
}

namespace OccurrenceGrammars {

namespace {

struct Definition {
    const TSLanguage *(*language)();
    // Concatenated in this order, as the highlighting queries are.
    QStringList queryResources;
};

struct Loaded {
    std::unique_ptr<HighlightQuery> query;
    std::unique_ptr<OccurrenceGrammar> grammar; // nullptr if the query is invalid
};

const std::map<QString, Definition> &definitions()
{
    static const std::map<QString, Definition> table = {
        {"c", {tree_sitter_c, {":/queries/c/occurrences.scm"}}},
        {"cpp", {tree_sitter_cpp, {":/queries/c/occurrences.scm", ":/queries/cpp/occurrences.scm"}}},
        {"java", {tree_sitter_java, {":/queries/java/occurrences.scm"}}},
        {"javascript",
         {tree_sitter_javascript,
          {":/queries/javascript/occurrences.scm", ":/queries/javascript/occurrences-params.scm"}}},
        {"typescript",
         {tree_sitter_typescript, {":/queries/javascript/occurrences.scm", ":/queries/typescript/occurrences.scm"}}},
        {"tsx", {tree_sitter_tsx, {":/queries/javascript/occurrences.scm", ":/queries/typescript/occurrences.scm"}}},
        {"python", {tree_sitter_python, {":/queries/python/occurrences.scm"}}},
    };
    return table;
}

QByteArray readSources(const QStringList &resources)
{
    QByteArray source;
    for (const QString &resource : resources) {
        QFile file(resource);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "Occurrence query not found:" << resource;
            continue;
        }
        source += file.readAll() + '\n';
    }
    return source;
}

Loaded load(const QString &languageId, const Definition &definition)
{
    Loaded loaded;
    const TSLanguage *language = definition.language();
    loaded.query = std::make_unique<HighlightQuery>(language, readSources(definition.queryResources));
    if (loaded.query->isValid())
        loaded.grammar = std::make_unique<OccurrenceGrammar>(OccurrenceGrammar{language, *loaded.query});
    else
        qWarning().noquote() << "Invalid occurrence query" << languageId << ":" << loaded.query->errorMessage();
    return loaded;
}

} // namespace

const OccurrenceGrammar *forLanguage(const QString &languageId)
{
    static std::map<QString, Loaded> loaded;
    static std::mutex loadedMutex;

    {
        const std::scoped_lock lock(loadedMutex);
        if (const auto it = loaded.find(languageId); it != loaded.end())
            return it->second.grammar.get();
    }
    const auto definition = definitions().find(languageId);
    if (definition == definitions().end())
        return nullptr;
    // Compiled without the lock, as TreeSitterGrammars does.
    Loaded entry = load(languageId, definition->second);
    const std::scoped_lock lock(loadedMutex);
    const auto [kept, isNew] = loaded.try_emplace(languageId);
    if (isNew)
        kept->second = std::move(entry);
    return kept->second.grammar.get();
}

} // namespace OccurrenceGrammars
