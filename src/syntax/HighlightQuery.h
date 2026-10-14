#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <tree_sitter/api.h>

#include <optional>
#include <vector>

// Requête tree-sitter de coloration (highlights.scm) compilée pour un langage.
//
// L'API C de tree-sitter ne fait qu'analyser les prédicats (#eq?, #match?...) :
// c'est à l'appelant de les évaluer. Cette classe les précompile et expose
// matchSatisfiesPredicates() pour filtrer les correspondances.
class HighlightQuery {
public:
    HighlightQuery(const TSLanguage *language, const QByteArray &source);
    ~HighlightQuery();

    HighlightQuery(const HighlightQuery &) = delete;
    HighlightQuery &operator=(const HighlightQuery &) = delete;

    bool isValid() const { return query_ != nullptr; }
    const QString &errorMessage() const { return errorMessage_; }

    const TSQuery *query() const { return query_; }
    uint32_t captureCount() const;
    QString captureName(uint32_t captureIndex) const;

    // `documentText` : texte UTF-8 complet du document analysé.
    bool matchSatisfiesPredicates(const TSQueryMatch &match, const char *documentText) const;

private:
    struct Predicate {
        enum class Kind { Equals, Matches, AnyOf };
        Kind kind;
        bool negated = false;
        uint32_t captureId = 0;
        std::optional<uint32_t> otherCaptureId; // #eq? @a @b
        QStringList literals;                   // #eq? @a "x", #any-of? @a "x" "y"
        QRegularExpression regex;               // #match? @a "regex"
    };

    void compilePredicates();
    QString stringValue(uint32_t stringId) const;
    bool isSatisfied(const Predicate &predicate, const TSQueryMatch &match, const char *documentText) const;

    TSQuery *query_ = nullptr;
    QString errorMessage_;
    std::vector<std::vector<Predicate>> predicatesByPattern_;
};
