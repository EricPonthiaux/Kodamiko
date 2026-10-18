#include "TextOutlines.h"

#include <QRegularExpression>

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

RangedSymbol lineSymbol(const QString &name, SymbolKind kind, int position, int startLine, int endLine)
{
    RangedSymbol ranged;
    ranged.symbol.name = name;
    ranged.symbol.kind = kind;
    ranged.symbol.position = position;
    ranged.symbol.startLine = startLine;
    ranged.symbol.endLine = endLine;
    ranged.start = startLine;
    ranged.end = endLine + 1;
    return ranged;
}

} // namespace

std::vector<OutlineSymbol> MarkdownOutline::symbols(const QByteArray &text)
{
    static const QRegularExpression fence(QStringLiteral("^ {0,3}(`{3,}|~{3,})"));
    static const QRegularExpression atx(QStringLiteral("^ {0,3}(#{1,6})(?:[ \\t]+(.*?))?(?:[ \\t]+#+)?[ \\t]*$"));
    static const QRegularExpression setext(QStringLiteral("^ {0,3}(=+|-+)[ \\t]*$"));
    static const QRegularExpression listOrQuote(QStringLiteral("^\\s*([-*+>]|\\d+[.)])(\\s|$)"));

    struct Heading {
        int level;
        int line;
        QString name;
        int position;
    };

    const std::vector<Line> lines = splitLines(text);
    std::vector<Heading> headings;
    QString openFence; // delimiter of the open code block
    bool previousIsParagraph = false;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        const Line &line = lines[i];
        if (const QRegularExpressionMatch match = fence.match(line.text); match.hasMatch()) {
            const QString marker = match.captured(1);
            if (openFence.isEmpty())
                openFence = marker;
            else if (marker.front() == openFence.front() && marker.size() >= openFence.size())
                openFence.clear();
            previousIsParagraph = false;
            continue;
        }
        if (!openFence.isEmpty())
            continue;

        if (const QRegularExpressionMatch match = atx.match(line.text); match.hasMatch()) {
            const QString name = match.captured(2).trimmed();
            if (!name.isEmpty())
                headings.push_back(
                    {static_cast<int>(match.capturedLength(1)), i, name, bytePosition(line, match.capturedStart(2))});
            previousIsParagraph = false;
            continue;
        }
        if (previousIsParagraph && setext.match(line.text).hasMatch()) {
            const Line &title = lines[i - 1];
            headings.push_back({line.text.trimmed().startsWith(QLatin1Char('=')) ? 1 : 2, i - 1, title.text.trimmed(),
                                title.position});
            previousIsParagraph = false;
            continue;
        }
        previousIsParagraph = !line.text.trimmed().isEmpty() && !listOrQuote.match(line.text).hasMatch();
    }

    // A section extends to the next heading of the same or a higher level.
    std::vector<RangedSymbol> flat;
    const int lastLine = static_cast<int>(lines.size()) - 1;
    for (size_t i = 0; i < headings.size(); ++i) {
        int endLine = lastLine;
        for (size_t j = i + 1; j < headings.size(); ++j) {
            if (headings[j].level <= headings[i].level) {
                endLine = headings[j].line - 1;
                break;
            }
        }
        flat.push_back(lineSymbol(headings[i].name, SymbolKind::Heading, headings[i].position, headings[i].line,
                                  std::max(endLine, headings[i].line)));
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
            flat.push_back(lineSymbol(match.captured(2), kind, bytePosition(line, match.capturedStart(2)), i,
                                      static_cast<int>(lines.size()) - 1));
        } else if (blockEnd.match(line.text).hasMatch() && !openBlocks.empty()) {
            RangedSymbol &open = flat[openBlocks.back()];
            open.symbol.endLine = i;
            open.end = i + 1;
            openBlocks.pop_back();
        } else if (const QRegularExpressionMatch match = target.match(line.text); match.hasMatch()) {
            flat.push_back(
                lineSymbol(match.captured(2), SymbolKind::Target, bytePosition(line, match.capturedStart(2)), i, i));
        }
    }
    return OutlineSymbols::nest(std::move(flat));
}
