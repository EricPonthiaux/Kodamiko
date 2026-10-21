// SPDX-License-Identifier: GPL-3.0-only

#include "ParsedText.h"

#include "index/OccurrenceGrammars.h"

#include <QByteArrayView>

#include <cctype>

namespace {

bool isIdentifierByte(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// `word` replaced by `replacement`, of the same length, wherever it is a
// whole word: the positions do not move.
void replaceWord(QByteArray &text, const QByteArray &word, const QByteArray &replacement)
{
    for (qsizetype at = text.indexOf(word); at >= 0; at = text.indexOf(word, at + word.size())) {
        const qsizetype end = at + word.size();
        const bool startsWord = at == 0 || !isIdentifierByte(text[at - 1]);
        const bool endsWord = end >= text.size() || !isIdentifierByte(text[end]);
        if (startsWord && endsWord)
            text.replace(at, word.size(), replacement);
    }
}

QByteArray parsableCpp(QByteArray text)
{
    for (const QByteArrayView macro : {"Q_OBJECT", "Q_GADGET", "Q_NAMESPACE"})
        replaceWord(text, macro.toByteArray(), QByteArray(macro.size(), ' '));
    replaceWord(text, "signals", "public ");
    replaceWord(text, "slots", "     ");
    return text;
}

} // namespace

ParsedText::ParsedText(const QString &languageId, const QString &text)
    : bytes_(text.toUtf8())
{
    const OccurrenceGrammar *grammar = OccurrenceGrammars::forLanguage(languageId);
    if (!grammar)
        return;
    const QByteArray parsable = languageId == QLatin1String("cpp") ? parsableCpp(bytes_) : bytes_;
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, grammar->language);
    tree_.reset(ts_parser_parse_string(parser, nullptr, parsable.constData(), std::uint32_t(parsable.size())));
    ts_parser_delete(parser);
}

QString ParsedText::textOf(TSNode node) const
{
    return textOf(ts_node_start_byte(node), ts_node_end_byte(node));
}

QString ParsedText::textOf(std::uint32_t start, std::uint32_t end) const
{
    return QString::fromUtf8(bytes_.constData() + start, qsizetype(end) - start);
}

LspProtocol::Position ParsedText::positionAt(std::uint32_t offset) const
{
    const qsizetype lineStart = bytes_.lastIndexOf('\n', qsizetype(offset) - 1) + 1;
    const int line = int(bytes_.left(offset).count('\n'));
    return {line, int(textOf(std::uint32_t(lineStart), offset).size())};
}

std::uint32_t ParsedText::offsetAt(const LspProtocol::Position &position) const
{
    qsizetype lineStart = 0;
    for (int line = 0; line < position.line && lineStart >= 0; ++line) {
        lineStart = bytes_.indexOf('\n', lineStart);
        if (lineStart >= 0)
            ++lineStart;
    }
    if (lineStart < 0)
        return std::uint32_t(bytes_.size());
    qsizetype lineEnd = bytes_.indexOf('\n', lineStart);
    if (lineEnd < 0)
        lineEnd = bytes_.size();
    const QString line = textOf(std::uint32_t(lineStart), std::uint32_t(lineEnd));
    return std::uint32_t(lineStart + line.left(position.character).toUtf8().size());
}

LspProtocol::Range ParsedText::rangeOf(TSNode node) const
{
    return {positionAt(ts_node_start_byte(node)), positionAt(ts_node_end_byte(node))};
}
