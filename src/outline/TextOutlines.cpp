#include "TextOutlines.h"

#include <QRegularExpression>

#include <algorithm>
#include <optional>

namespace {

struct Line {
    QString text; // without line ending
    int position; // byte offset of the start of the line
};

std::vector<Line> splitLines(const QByteArray &text)
{
    std::vector<Line> lines;
    qsizetype start = 0;
    while (start <= text.size()) {
        qsizetype end = text.indexOf('\n', start);
        if (end < 0)
            end = text.size();
        qsizetype contentEnd = end;
        if (contentEnd > start && text[contentEnd - 1] == '\r')
            --contentEnd;
        lines.push_back({QString::fromUtf8(text.constData() + start, contentEnd - start), static_cast<int>(start)});
        start = end + 1;
    }
    return lines;
}

// Byte offset of character `index` in a line.
int bytePosition(const Line &line, qsizetype index)
{
    return line.position + static_cast<int>(QStringView(line.text).left(index).toUtf8().size());
}

// First and last line (inclusive) covered by a symbol.
struct LineSpan {
    int first{};
    int last{};
};

RangedSymbol lineSymbol(const QString &name, SymbolKind kind, int position, LineSpan lines)
{
    RangedSymbol ranged;
    ranged.symbol.name = name;
    ranged.symbol.kind = kind;
    ranged.symbol.position = position;
    ranged.symbol.startLine = lines.first;
    ranged.symbol.endLine = lines.last;
    ranged.start = lines.first;
    ranged.end = lines.last + 1;
    return ranged;
}

struct Heading {
    int level{};
    int line{};
    QString name;
    int position{};
};

// Opens or closes the code block when `line` is a fence; returns whether it is one.
bool readFence(const Line &line, QString &openFence)
{
    static const QRegularExpression fence(QStringLiteral("^ {0,3}(`{3,}|~{3,})"));

    const QRegularExpressionMatch match = fence.match(line.text);
    if (!match.hasMatch())
        return false;
    const QString marker = match.captured(1);
    if (openFence.isEmpty())
        openFence = marker;
    else if (marker.front() == openFence.front() && marker.size() >= openFence.size())
        openFence.clear();
    return true;
}

// What a line outside code blocks contributes: maybe a heading, and whether
// it is paragraph text (which a setext underline on the next line turns into a heading).
struct LineReading {
    std::optional<Heading> heading;
    bool isParagraph = false;
};

LineReading readLine(const std::vector<Line> &lines, int index, bool previousIsParagraph)
{
    static const QRegularExpression atx(QStringLiteral("^ {0,3}(#{1,6})(?:[ \\t]+(.*?))?(?:[ \\t]+#+)?[ \\t]*$"));
    static const QRegularExpression setext(QStringLiteral("^ {0,3}(=+|-+)[ \\t]*$"));
    static const QRegularExpression listOrQuote(QStringLiteral("^\\s*([-*+>]|\\d+[.)])(\\s|$)"));

    const Line &line = lines[index];
    if (const QRegularExpressionMatch match = atx.match(line.text); match.hasMatch()) {
        const QString name = match.captured(2).trimmed();
        if (name.isEmpty())
            return {};
        const int level = static_cast<int>(match.capturedLength(1));
        return {Heading{level, index, name, bytePosition(line, match.capturedStart(2))}};
    }
    if (previousIsParagraph && setext.match(line.text).hasMatch()) {
        const Line &title = lines[index - 1];
        const int level = line.text.trimmed().startsWith(QLatin1Char('=')) ? 1 : 2;
        return {Heading{level, index - 1, title.text.trimmed(), title.position}};
    }
    const bool isParagraph = !line.text.trimmed().isEmpty() && !listOrQuote.match(line.text).hasMatch();
    return {std::nullopt, isParagraph};
}

std::vector<Heading> markdownHeadings(const std::vector<Line> &lines)
{
    std::vector<Heading> headings;
    QString openFence; // delimiter of the open code block
    bool previousIsParagraph = false;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        if (readFence(lines[i], openFence)) {
            previousIsParagraph = false;
            continue;
        }
        if (!openFence.isEmpty())
            continue;

        LineReading reading = readLine(lines, i, previousIsParagraph);
        if (reading.heading)
            headings.push_back(std::move(*reading.heading));
        previousIsParagraph = reading.isParagraph;
    }
    return headings;
}

// A section extends to the next heading of the same or a higher level.
int sectionLastLine(const std::vector<Heading> &headings, size_t index, int lastLine)
{
    const Heading &heading = headings[index];
    const auto next = std::find_if(headings.begin() + static_cast<std::ptrdiff_t>(index) + 1, headings.end(),
                                   [&](const Heading &other) { return other.level <= heading.level; });
    const int endLine = next == headings.end() ? lastLine : next->line - 1;
    return std::max(endLine, heading.line);
}

} // namespace

std::vector<OutlineSymbol> MarkdownOutline::symbols(const QByteArray &text)
{
    const std::vector<Line> lines = splitLines(text);
    const std::vector<Heading> headings = markdownHeadings(lines);

    std::vector<RangedSymbol> flat;
    const int lastLine = static_cast<int>(lines.size()) - 1;
    for (size_t i = 0; i < headings.size(); ++i) {
        const Heading &heading = headings[i];
        flat.push_back(lineSymbol(heading.name, SymbolKind::Heading, heading.position,
                                  {heading.line, sectionLastLine(headings, i, lastLine)}));
    }
    return OutlineSymbols::nest(std::move(flat));
}

std::vector<OutlineSymbol> CMakeOutline::symbols(const QByteArray &text)
{
    static const QRegularExpression block(QStringLiteral("^\\s*(function|macro)\\s*\\(\\s*([^\\s()]+)"),
                                          QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression blockEnd(QStringLiteral("^\\s*end(function|macro)\\b"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression target(QStringLiteral("^\\s*((?:qt_)?add_(?:executable|library)|add_custom_"
                                                          "target)\\s*\\(\\s*([^\\s()]+)"),
                                           QRegularExpression::CaseInsensitiveOption);

    const std::vector<Line> lines = splitLines(text);
    std::vector<RangedSymbol> flat;
    std::vector<size_t> openBlocks; // function/macro without endfunction/endmacro yet
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        const Line &line = lines[i];
        if (const QRegularExpressionMatch match = block.match(line.text); match.hasMatch()) {
            const SymbolKind kind = match.captured(1).compare(QLatin1String("macro"), Qt::CaseInsensitive) == 0
                                        ? SymbolKind::Macro
                                        : SymbolKind::Function;
            openBlocks.push_back(flat.size());
            flat.push_back(lineSymbol(match.captured(2), kind, bytePosition(line, match.capturedStart(2)),
                                      {i, static_cast<int>(lines.size()) - 1}));
        } else if (blockEnd.match(line.text).hasMatch() && !openBlocks.empty()) {
            RangedSymbol &open = flat[openBlocks.back()];
            open.symbol.endLine = i;
            open.end = i + 1;
            openBlocks.pop_back();
        } else if (const QRegularExpressionMatch targetMatch = target.match(line.text); targetMatch.hasMatch()) {
            flat.push_back(lineSymbol(targetMatch.captured(2), SymbolKind::Target,
                                      bytePosition(line, targetMatch.capturedStart(2)), {i, i}));
        }
    }
    return OutlineSymbols::nest(std::move(flat));
}
