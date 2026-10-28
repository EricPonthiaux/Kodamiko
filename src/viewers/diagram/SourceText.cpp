// SPDX-License-Identifier: GPL-3.0-only

#include "SourceText.h"

#include <QRegularExpression>

namespace diagram::source {

namespace {

// Skips the PlantUML blocks that only style the drawing: <style>…</style>
// and skinparam { … }. Fed the lines in order.
class StyleBlockSkipper {
public:
    bool skips(const QString &line) { return skipsStyle(line) || skipsSkinparam(line); }

private:
    bool skipsStyle(const QString &line)
    {
        static const QRegularExpression styleOpen(QStringLiteral("^<style\\b"),
                                                  QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression styleClose(QStringLiteral("</style>"),
                                                   QRegularExpression::CaseInsensitiveOption);
        if (styleOpen.match(line).hasMatch()) {
            inStyle_ = true;
            return true;
        }
        if (!inStyle_)
            return false;
        if (styleClose.match(line).hasMatch())
            inStyle_ = false;
        return true;
    }

    bool skipsSkinparam(const QString &line)
    {
        static const QRegularExpression skinparam(QStringLiteral("^skinparam\\b"),
                                                  QRegularExpression::CaseInsensitiveOption);
        if (skinparamDepth_ > 0) {
            if (line.contains(QLatin1Char('{')))
                ++skinparamDepth_;
            if (line.contains(QLatin1Char('}')))
                --skinparamDepth_;
            return true;
        }
        if (!skinparam.match(line).hasMatch())
            return false;
        if (line.contains(QLatin1Char('{')) && !line.contains(QLatin1Char('}')))
            skinparamDepth_ = 1;
        return true;
    }

    bool inStyle_ = false;
    int skinparamDepth_ = 0;
};

bool isQuote(QChar c)
{
    return c == QLatin1Char('"') || c == QLatin1Char('\'');
}

bool isOpeningBracket(QChar c)
{
    return c == QLatin1Char('[') || c == QLatin1Char('(') || c == QLatin1Char('{');
}

bool isClosingBracket(QChar c)
{
    return c == QLatin1Char(']') || c == QLatin1Char(')') || c == QLatin1Char('}');
}

QStringList trimmedNonEmpty(const QStringList &parts)
{
    QStringList result;
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            result.append(trimmed);
    }
    return result;
}

} // namespace

QStringList mermaidLines(const QString &source, bool keepIndent)
{
    QStringList lines;
    static const QRegularExpression newline(QStringLiteral("\\r?\\n"));
    static const QRegularExpression comment(QStringLiteral("%%.*$"));
    for (QString raw : source.split(newline)) {
        raw.remove(comment);
        QString line = keepIndent ? raw : raw.trimmed();
        if (keepIndent) {
            while (!line.isEmpty() && line.back().isSpace())
                line.chop(1);
        }
        if (!line.trimmed().isEmpty())
            lines.append(line);
    }
    return lines;
}

QString mermaidQuote(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('"'), QStringLiteral("#quot;"));
    escaped.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString decodeLabel(const QString &text)
{
    QString result = text;
    static const QRegularExpression br(QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption);
    result.replace(br, QStringLiteral("\n"));
    result.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    static const QRegularExpression numeric(QStringLiteral("&?#(\\d+);"));
    QString decoded;
    qsizetype last = 0;
    for (auto it = numeric.globalMatch(result); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        decoded += result.mid(last, m.capturedStart() - last);
        const uint code = m.captured(1).toUInt();
        decoded += code ? QString::fromUcs4(reinterpret_cast<const char32_t *>(&code), 1) : QString();
        last = m.capturedEnd();
    }
    decoded += result.mid(last);
    result = std::move(decoded);
    static const QRegularExpression quot(QStringLiteral("&quot;|#quot;"));
    static const QRegularExpression apos(QStringLiteral("&#39;|&apos;|#39;"));
    static const QRegularExpression lt(QStringLiteral("&lt;"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression gt(QStringLiteral("&gt;"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression nbsp(QStringLiteral("&nbsp;"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression amp(QStringLiteral("&amp;"), QRegularExpression::CaseInsensitiveOption);
    result.replace(quot, QStringLiteral("\""));
    result.replace(apos, QStringLiteral("'"));
    result.replace(lt, QStringLiteral("<"));
    result.replace(gt, QStringLiteral(">"));
    result.replace(nbsp, QStringLiteral(" "));
    result.replace(amp, QStringLiteral("&"));
    return result;
}

QString mermaidUnquote(const QString &text)
{
    QString t = text.trimmed();
    if (t.startsWith(QStringLiteral("\"`")) && t.endsWith(QStringLiteral("`\"")) && t.size() >= 4)
        t = t.mid(2, t.size() - 4);
    else if (t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"')) && t.size() >= 2)
        t = t.mid(1, t.size() - 2);
    return decodeLabel(t);
}

QString pipeLabel(const QString &text)
{
    QString result = text;
    result.replace(QLatin1Char('|'), QLatin1Char('/'));
    result.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return result;
}

std::optional<CodeLinkStatement> readMermaidCodeLink(const QString &statement)
{
    static const QRegularExpression click(QStringLiteral("^click\\s+(\\S+)\\s+(?:href\\s+)?\"([^\"]*)\""));
    const QRegularExpressionMatch match = click.match(statement);
    if (!match.hasMatch())
        return std::nullopt;
    return CodeLinkStatement{.key = match.captured(1), .target = match.captured(2).trimmed()};
}

QString mermaidCodeLinkLine(const QString &key, const QString &target, const QString &indent)
{
    QString written = target;
    written.replace(QLatin1Char('"'), QLatin1Char('\''));
    return QStringLiteral("%1click %2 href \"%3\"").arg(indent, key, written);
}

QString plantumlText(const QString &text)
{
    QString result = text;
    result.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    return result;
}

QStringList plantumlLines(const QString &source)
{
    static const QRegularExpression block(QStringLiteral("/'[\\s\\S]*?'/"));
    static const QRegularExpression noise(
        QStringLiteral("^(?:@startuml|@enduml|title\\b|caption\\b|header\\b|footer\\b|legend\\b|endlegend\\b|hide\\b|"
                       "show\\b|remove\\b|restore\\b|scale\\b|autonumber\\b|newpage\\b|mainframe\\b|"
                       "left to right direction|top to bottom direction|allow_mixing\\b|!\\w)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression newline(QStringLiteral("\\r?\\n"));
    QString text = source;
    text.remove(block);
    QStringList lines;
    StyleBlockSkipper styleBlocks;
    for (const QString &raw : text.split(newline)) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('\'')))
            continue;
        if (styleBlocks.skips(line))
            continue;
        if (noise.match(line).hasMatch())
            continue;
        lines.append(line);
    }
    return lines;
}

QString plantumlName(const QString &text)
{
    static const QRegularExpression simple(QStringLiteral("^[\\w.]+$"), QRegularExpression::UseUnicodePropertiesOption);
    if (simple.match(text).hasMatch())
        return text;
    QString quoted = text;
    quoted.replace(QLatin1Char('"'), QLatin1Char('\''));
    quoted.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

PlantumlLinked splitPlantumlCodeLink(const QString &statement)
{
    // [[target]], [[target{tooltip}]] or [[target label]].
    static const QRegularExpression link(QStringLiteral("\\s*\\[\\[([^\\]\\s{]*)[^\\]]*\\]\\]"));
    const QRegularExpressionMatch match = link.match(statement);
    if (!match.hasMatch())
        return {.statement = statement, .target = {}};
    return {.statement = QString(statement).remove(match.capturedStart(), match.capturedLength()),
            .target = match.captured(1)};
}

QString plantumlCodeLink(const QString &target)
{
    return target.isEmpty() ? QString() : QStringLiteral(" [[%1]]").arg(target);
}

const QList<UmlRelation> &umlRelations()
{
    static const QList<UmlRelation> list{
        {QStringLiteral("<|--"), QStringLiteral("extension"), true},
        {QStringLiteral("--|>"), QStringLiteral("extension"), false},
        {QStringLiteral("<|.."), QStringLiteral("realization"), true},
        {QStringLiteral("..|>"), QStringLiteral("realization"), false},
        {QStringLiteral("*--"), QStringLiteral("composition"), true},
        {QStringLiteral("--*"), QStringLiteral("composition"), false},
        {QStringLiteral("o--"), QStringLiteral("aggregation"), true},
        {QStringLiteral("--o"), QStringLiteral("aggregation"), false},
        {QStringLiteral("<--"), QStringLiteral("association"), true},
        {QStringLiteral("-->"), QStringLiteral("association"), false},
        {QStringLiteral("<.."), QStringLiteral("dependency"), true},
        {QStringLiteral("..>"), QStringLiteral("dependency"), false},
        {QStringLiteral(".."), QStringLiteral("dotted"), false},
        {QStringLiteral("--"), QStringLiteral("link"), false},
    };
    return list;
}

QString umlRelationPattern()
{
    QStringList tokens;
    for (const UmlRelation &relation : umlRelations())
        tokens.append(QRegularExpression::escape(relation.token));
    return tokens.join(QLatin1Char('|'));
}

QString umlRelationOutput(const QString &kind)
{
    static const QHash<QString, QString> output{
        {QStringLiteral("extension"), QStringLiteral("--|>")},  {QStringLiteral("realization"), QStringLiteral("..|>")},
        {QStringLiteral("composition"), QStringLiteral("--*")}, {QStringLiteral("aggregation"), QStringLiteral("--o")},
        {QStringLiteral("association"), QStringLiteral("-->")}, {QStringLiteral("dependency"), QStringLiteral("..>")},
        {QStringLiteral("dotted"), QStringLiteral("..")},       {QStringLiteral("link"), QStringLiteral("--")},
    };
    return output.value(kind, QStringLiteral("-->"));
}

QString keyFor(const QString &key, const QString &text, QSet<QString> &used)
{
    QString base = key;
    if (base.isEmpty()) {
        QString slug = text.normalized(QString::NormalizationForm_D);
        static const QRegularExpression marks(QStringLiteral("[\\x{0300}-\\x{036f}]"));
        static const QRegularExpression other(QStringLiteral("[^A-Za-z0-9]+"));
        static const QRegularExpression edges(QStringLiteral("^_+|_+$"));
        slug.remove(marks);
        slug.replace(other, QStringLiteral("_"));
        slug.remove(edges);
        base = slug.left(24);
        if (base.isEmpty())
            base = QStringLiteral("n");
    }
    QString unique = base;
    for (int i = 2; used.contains(unique); ++i)
        unique = base + QString::number(i);
    used.insert(unique);
    return unique;
}

QStringList splitTop(const QString &text, QChar separator)
{
    QStringList parts;
    int depth = 0;
    QChar quote;
    qsizetype start = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            continue;
        }
        if (isQuote(c)) {
            quote = c;
        } else if (isOpeningBracket(c)) {
            ++depth;
        } else if (isClosingBracket(c)) {
            depth = std::max(0, depth - 1);
        } else if (c == separator && depth == 0) {
            parts.append(text.mid(start, i - start));
            start = i + 1;
        }
    }
    parts.append(text.mid(start));
    return trimmedNonEmpty(parts);
}

} // namespace diagram::source
