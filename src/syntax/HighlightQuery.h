#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <tree_sitter/api.h>

#include <optional>
#include <vector>

// Tree-sitter highlighting query (highlights.scm) compiled for a language.
//
// The tree-sitter C API only parses predicates (#eq?, #match?...): evaluating
// them is up to the caller. This class precompiles them and exposes
// matchSatisfiesPredicates() to filter matches.
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

    // `documentText`: full UTF-8 text of the parsed document.
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
