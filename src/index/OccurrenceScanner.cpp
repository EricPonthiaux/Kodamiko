// SPDX-License-Identifier: GPL-3.0-only

#include "OccurrenceScanner.h"

#include "OccurrenceGrammars.h"
#include "syntax/HighlightQuery.h"

#include <QHash>

#include <cstring>
#include <memory>
#include <utility>

namespace {

// A word read from a comment, a string or a text has this many bytes at
// least: shorter ones are noise there.
constexpr int minimumProseWordLength = 2;
// Deeper than this (generated code), a node's words are read as text.
constexpr int maximumDepth = 1000;

// What the query says about the identifiers and imports of a file.
struct Captures {
    QHash<std::uint32_t, OccurrenceRole> roles; // by start byte
    QList<std::pair<std::uint32_t, std::uint32_t>> imports;
};

// A part of a text, and the line it starts on.
struct Span {
    std::uint32_t from = 0;
    std::uint32_t to = 0;
    std::uint32_t line = 0;
};

// Where words are read and kept.
struct Reading {
    const QByteArray &text;
    const Captures &captures;
    QList<Occurrence> &occurrences;
};

bool isWordByte(char c)
{
    const auto byte = static_cast<unsigned char>(c);
    return byte >= 0x80 || byte == '_' || (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'z') ||
           (byte >= 'A' && byte <= 'Z');
}

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

// The words of `span` in `text`.
void appendWords(const QByteArray &text, Span span, OccurrenceRole role, QList<Occurrence> &occurrences)
{
    const char *data = text.constData();
    const std::uint32_t to = span.to;
    std::uint32_t line = span.line;
    std::uint32_t i = span.from;
    while (i < to) {
        if (data[i] == '\n') {
            ++line;
            ++i;
            continue;
        }
        if (!isWordByte(data[i])) {
            ++i;
            continue;
        }
        const std::uint32_t start = i;
        while (i < to && isWordByte(data[i]))
            ++i;
        // A path's words count whatever their length ("x.h").
        const auto minimumLength = role == OccurrenceRole::Import ? 1 : minimumProseWordLength;
        const bool isWord = !isDigit(data[start]) && i - start >= std::uint32_t(minimumLength);
        if (isWord)
            occurrences.append({QByteArray(data + start, i - start), start, line, role});
    }
}

OccurrenceRole roleOfCapture(const QString &name)
{
    if (name == QLatin1String("definition"))
        return OccurrenceRole::Definition;
    if (name == QLatin1String("call"))
        return OccurrenceRole::Call;
    return OccurrenceRole::Write;
}

// A definition wins over a write, a write over a call.
int precedenceOf(OccurrenceRole role)
{
    switch (role) {
    case OccurrenceRole::Definition:
        return 3;
    case OccurrenceRole::Write:
        return 2;
    case OccurrenceRole::Call:
        return 1;
    case OccurrenceRole::Reference:
    case OccurrenceRole::Import:
    case OccurrenceRole::Comment:
    case OccurrenceRole::String:
    case OccurrenceRole::Text:
        break;
    }
    return 0;
}

void addCapture(Captures &captures, const QString &name, TSNode node)
{
    const std::uint32_t start = ts_node_start_byte(node);
    if (name == QLatin1String("import")) {
        captures.imports.append({start, ts_node_end_byte(node)});
        return;
    }
    const OccurrenceRole role = roleOfCapture(name);
    const auto known = captures.roles.constFind(start);
    if (known == captures.roles.cend() || precedenceOf(role) > precedenceOf(*known))
        captures.roles.insert(start, role);
}

Captures runQuery(const HighlightQuery &query, TSQueryCursor *cursor, TSNode root, const QByteArray &text)
{
    Captures captures;
    ts_query_cursor_exec(cursor, query.query(), root);
    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match)) {
        if (!query.matchSatisfiesPredicates(match, text.constData()))
            continue;
        for (uint16_t i = 0; i < match.capture_count; ++i)
            addCapture(captures, query.captureName(match.captures[i].index), match.captures[i].node);
    }
    return captures;
}

bool isImport(const Captures &captures, std::uint32_t start, std::uint32_t end)
{
    return captures.imports.contains(std::pair{start, end});
}

void visit(const Reading &reading, TSNode node, bool inString, int depth);

void visitChildren(const Reading &reading, TSNode node, bool inString, int depth)
{
    const uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i)
        visit(reading, ts_node_child(node, i), inString, depth + 1);
}

// A leaf: an identifier, or a piece of a string (its words).
void visitLeaf(const Reading &reading, TSNode node, bool inString)
{
    const char *type = ts_node_type(node);
    const std::uint32_t start = ts_node_start_byte(node);
    const std::uint32_t end = ts_node_end_byte(node);
    const std::uint32_t line = ts_node_start_point(node).row;
    const std::size_t typeLength = std::strlen(type);
    constexpr std::string_view identifier = "identifier";
    const bool isIdentifier =
        typeLength >= identifier.size() && std::string_view(type + typeLength - identifier.size()) == identifier;
    if (isIdentifier && end > start) {
        const OccurrenceRole role = reading.captures.roles.value(start, OccurrenceRole::Reference);
        reading.occurrences.append({reading.text.mid(start, end - start), start, line, role});
    } else if (inString || std::string_view(type) == "jsx_text") {
        // JSX text is shown as it is written: a string.
        appendWords(reading.text, {start, end, line}, OccurrenceRole::String, reading.occurrences);
    } else if (std::string_view(type) == "preproc_arg") {
        // A macro's body: its words name what it uses.
        appendWords(reading.text, {start, end, line}, OccurrenceRole::Reference, reading.occurrences);
    }
}

void visit(const Reading &reading, TSNode node, bool inString, int depth)
{
    const std::uint32_t start = ts_node_start_byte(node);
    const std::uint32_t end = ts_node_end_byte(node);
    const std::uint32_t line = ts_node_start_point(node).row;
    const std::string_view type = ts_node_type(node);
    if (isImport(reading.captures, start, end)) {
        appendWords(reading.text, {start, end, line}, OccurrenceRole::Import, reading.occurrences);
        return;
    }
    if (type.find("comment") != std::string_view::npos) {
        appendWords(reading.text, {start, end, line}, OccurrenceRole::Comment, reading.occurrences);
        return;
    }
    if (depth > maximumDepth) {
        appendWords(reading.text, {start, end, line}, OccurrenceRole::Text, reading.occurrences);
        return;
    }
    if (ts_node_child_count(node) == 0) {
        visitLeaf(reading, node, inString);
        return;
    }
    const bool isString = type.find("string") != std::string_view::npos;
    visitChildren(reading, node, inString || isString, depth);
}

} // namespace

OccurrenceScanner::OccurrenceScanner()
    : parser_(ts_parser_new())
    , cursor_(ts_query_cursor_new())
{
}

OccurrenceScanner::~OccurrenceScanner()
{
    ts_query_cursor_delete(cursor_);
    ts_parser_delete(parser_);
}

QList<Occurrence> OccurrenceScanner::scan(const QString &languageId, const QByteArray &text)
{
    if (const OccurrenceGrammar *grammar = OccurrenceGrammars::forLanguage(languageId))
        return scanCode(*grammar, text);
    QList<Occurrence> occurrences;
    appendWords(text, {0, std::uint32_t(text.size()), 0}, OccurrenceRole::Text, occurrences);
    return occurrences;
}

QList<Occurrence> OccurrenceScanner::scanCode(const OccurrenceGrammar &grammar, const QByteArray &text)
{
    QList<Occurrence> occurrences;
    ts_parser_set_language(parser_, grammar.language);
    const std::unique_ptr<TSTree, decltype(&ts_tree_delete)> tree(
        ts_parser_parse_string(parser_, nullptr, text.constData(), std::uint32_t(text.size())), ts_tree_delete);
    if (!tree)
        return occurrences;
    const TSNode root = ts_tree_root_node(tree.get());
    const Captures captures = runQuery(grammar.query, cursor_, root, text);
    visit({text, captures, occurrences}, root, false, 0);
    return occurrences;
}
