#include "TreeSitterOutline.h"

#include "syntax/HighlightQuery.h"

#include <algorithm>
#include <cctype>
#include <optional>

namespace {

std::optional<SymbolKind> kindFromName(QStringView kind)
{
    if (kind == u"namespace")
        return SymbolKind::Namespace;
    if (kind == u"class")
        return SymbolKind::Class;
    if (kind == u"struct")
        return SymbolKind::Struct;
    if (kind == u"interface")
        return SymbolKind::Interface;
    if (kind == u"enum")
        return SymbolKind::Enum;
    if (kind == u"type")
        return SymbolKind::Type;
    if (kind == u"function")
        return SymbolKind::Function;
    if (kind == u"method")
        return SymbolKind::Method;
    if (kind == u"field")
        return SymbolKind::Field;
    if (kind == u"macro")
        return SymbolKind::Macro;
    return std::nullopt;
}

bool isIdentifierChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Replaces each standalone occurrence of `word` with spaces (positions
// do not change).
void blankWord(QByteArray &text, const QByteArray &word)
{
    for (qsizetype at = text.indexOf(word); at >= 0; at = text.indexOf(word, at + word.size())) {
        const qsizetype end = at + word.size();
        if ((at == 0 || !isIdentifierChar(text[at - 1])) && (end == text.size() || !isIdentifierChar(text[end])))
            std::fill(text.begin() + at, text.begin() + end, ' ');
    }
}

} // namespace

TreeSitterOutline::TreeSitterOutline(const TSLanguage *language,
                                     const HighlightQuery &query,
                                     QByteArrayList blankedMacros)
    : query_(query)
    , blankedMacros_(std::move(blankedMacros))
    , parser_(ts_parser_new())
    , cursor_(ts_query_cursor_new())
{
    ts_parser_set_language(parser_, language);
}

TreeSitterOutline::~TreeSitterOutline()
{
    ts_query_cursor_delete(cursor_);
    ts_parser_delete(parser_);
}

std::vector<OutlineSymbol> TreeSitterOutline::symbols(const QByteArray &originalText)
{
    QByteArray text = originalText;
    for (const QByteArray &macro : std::as_const(blankedMacros_))
        blankWord(text, macro);

    TSTree *tree = ts_parser_parse_string(parser_, nullptr, text.constData(), static_cast<uint32_t>(text.size()));
    ts_query_cursor_exec(cursor_, query_.query(), ts_tree_root_node(tree));

    std::vector<RangedSymbol> flat;
    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor_, &match)) {
        if (!query_.matchSatisfiesPredicates(match, text.constData()))
            continue;

        std::optional<TSNode> nameNode;
        std::optional<TSNode> definitionNode;
        std::optional<SymbolKind> kind;
        bool declarationOnly = false;
        for (uint16_t i = 0; i < match.capture_count; ++i) {
            const TSQueryCapture &capture = match.captures[i];
            const QString captureName = query_.captureName(capture.index);
            if (captureName == QLatin1String("name")) {
                nameNode = capture.node;
                continue;
            }
            const qsizetype dot = captureName.indexOf(QLatin1Char('.'));
            const QStringView role = QStringView(captureName).left(dot);
            if (dot < 0 || (role != u"definition" && role != u"declaration"))
                continue;
            kind = kindFromName(QStringView(captureName).mid(dot + 1));
            definitionNode = capture.node;
            declarationOnly = role == u"declaration";
        }
        if (!definitionNode || !kind)
            continue;

        RangedSymbol ranged;
        ranged.start = static_cast<int>(ts_node_start_byte(*definitionNode));
        ranged.end = static_cast<int>(ts_node_end_byte(*definitionNode));
        ranged.declarationOnly = declarationOnly;
        ranged.symbol.declarationOnly = declarationOnly;
        OutlineSymbol &symbol = ranged.symbol;
        symbol.kind = *kind;
        symbol.startLine = static_cast<int>(ts_node_start_point(*definitionNode).row);
        symbol.endLine = static_cast<int>(ts_node_end_point(*definitionNode).row);
        if (nameNode) {
            const uint32_t start = ts_node_start_byte(*nameNode);
            symbol.name = QString::fromUtf8(text.constData() + start, ts_node_end_byte(*nameNode) - start).simplified();
            symbol.position = static_cast<int>(start);
        } else {
            symbol.name = QStringLiteral("(anonymous)");
            symbol.position = ranged.start;
        }
        flat.push_back(std::move(ranged));
    }

    ts_tree_delete(tree);
    return OutlineSymbols::nest(std::move(flat));
}
