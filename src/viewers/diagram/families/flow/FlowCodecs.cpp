// Mermaid flowcharts (sidecar's fence/mermaid-flow.js) and Flowboard's .flow
// files.

#include "Flow.h"
#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/SourceText.h"
#include "viewers/diagram/families/Families.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <cmath>

namespace diagram::flow {

namespace {

using namespace diagram::source;

// Source delimiters, longest first (`((` before `(`), with the variant they
// produce. `[/a/]` and `[/a\]` differ only by their closer.
struct Delimiter {
    const char *open;
    const char *close;
    const char *variant;
};

const Delimiter delimiters[] = {
    {"(((", ")))", "doublecircle"},
    {"((", "))", "circle"},
    {"([", "])", "stadium"},
    {"[[", "]]", "subroutine"},
    {"[(", ")]", "cylinder"},
    {"(-", "-)", "ellipse"},
    {"{{", "}}", "hexagon"},
    {"[/", "/]", "leanr"},
    {"[/", "\\]", "trap"},
    {"[\\", "/]", "invtrap"},
    {"[\\", "\\]", "leanl"},
    {"[", "]", "square"},
    {"(", ")", "round"},
    {"{", "}", "diamond"},
    {">", "]", "odd"},
};

std::pair<QString, QString> delimitersOf(const QString &variant, const QString &type)
{
    for (const Delimiter &d : delimiters) {
        if (variant == QLatin1String(d.variant))
            return {QString::fromLatin1(d.open), QString::fromLatin1(d.close)};
    }
    if (type == QLatin1String("decision"))
        return {QStringLiteral("{"), QStringLiteral("}")};
    if (type == QLatin1String("terminator"))
        return {QStringLiteral("(["), QStringLiteral("])")};
    return {QStringLiteral("["), QStringLiteral("]")};
}

// Splits a statement into node groups and edge operators: the operator,
// then an optional |pipe label|.
const QRegularExpression &edgeSplit()
{
    static const QRegularExpression pattern(
        QStringLiteral("\\s*([<xo]?(?:={2,}[=>xo]?|-{2,}[->xo]?|-\\.+->?|-\\.+-|~{3,}))(?:\\s*\\|([^|]*)\\|)?\\s*"));
    return pattern;
}

const QRegularExpression &skip()
{
    static const QRegularExpression pattern(
        QStringLiteral("^(?:style|linkStyle|classDef|class|click|accTitle|accDescr|direction|subgraph)\\b|^end$"));
    return pattern;
}

// `flowchart` or `graph`, an optional renderer suffix, the direction.
const QRegularExpression &header()
{
    static const QRegularExpression pattern(QStringLiteral("^(?:flowchart|graph)(?:-\\w+)?\\b\\s*(TB|TD|BT|RL|LR)?\\b"),
                                            QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// A node id; Mermaid's are not ASCII-only (`Début` is legal).
const QString idPattern = QStringLiteral("[\\p{L}\\p{N}_](?:[\\p{L}\\p{N}_.-]*[\\p{L}\\p{N}_])?");

const QHash<QString, QString> &v11Shapes()
{
    static const QHash<QString, QString> shapes{
        {QStringLiteral("text"), QStringLiteral("text")},
        {QStringLiteral("rect"), QStringLiteral("square")},
        {QStringLiteral("rectangle"), QStringLiteral("square")},
        {QStringLiteral("proc"), QStringLiteral("square")},
        {QStringLiteral("process"), QStringLiteral("square")},
        {QStringLiteral("notch-rect"), QStringLiteral("square")},
        {QStringLiteral("card"), QStringLiteral("square")},
        {QStringLiteral("doc"), QStringLiteral("square")},
        {QStringLiteral("lin-rect"), QStringLiteral("square")},
        {QStringLiteral("rounded"), QStringLiteral("round")},
        {QStringLiteral("round"), QStringLiteral("round")},
        {QStringLiteral("event"), QStringLiteral("round")},
        {QStringLiteral("stadium"), QStringLiteral("stadium")},
        {QStringLiteral("pill"), QStringLiteral("stadium")},
        {QStringLiteral("terminal"), QStringLiteral("stadium")},
        {QStringLiteral("subproc"), QStringLiteral("subroutine")},
        {QStringLiteral("subprocess"), QStringLiteral("subroutine")},
        {QStringLiteral("framed-rectangle"), QStringLiteral("subroutine")},
        {QStringLiteral("cyl"), QStringLiteral("cylinder")},
        {QStringLiteral("cylinder"), QStringLiteral("cylinder")},
        {QStringLiteral("db"), QStringLiteral("cylinder")},
        {QStringLiteral("database"), QStringLiteral("cylinder")},
        {QStringLiteral("disk"), QStringLiteral("cylinder")},
        {QStringLiteral("circle"), QStringLiteral("circle")},
        {QStringLiteral("circ"), QStringLiteral("circle")},
        {QStringLiteral("dbl-circ"), QStringLiteral("doublecircle")},
        {QStringLiteral("double-circle"), QStringLiteral("doublecircle")},
        {QStringLiteral("diam"), QStringLiteral("diamond")},
        {QStringLiteral("diamond"), QStringLiteral("diamond")},
        {QStringLiteral("decision"), QStringLiteral("diamond")},
        {QStringLiteral("question"), QStringLiteral("diamond")},
        {QStringLiteral("hex"), QStringLiteral("hexagon")},
        {QStringLiteral("hexagon"), QStringLiteral("hexagon")},
        {QStringLiteral("prepare"), QStringLiteral("hexagon")},
        {QStringLiteral("lean-r"), QStringLiteral("leanr")},
        {QStringLiteral("lean-right"), QStringLiteral("leanr")},
        {QStringLiteral("in-out"), QStringLiteral("leanr")},
        {QStringLiteral("lean-l"), QStringLiteral("leanl")},
        {QStringLiteral("lean-left"), QStringLiteral("leanl")},
        {QStringLiteral("out-in"), QStringLiteral("leanl")},
        {QStringLiteral("trap-b"), QStringLiteral("trap")},
        {QStringLiteral("trapezoid-bottom"), QStringLiteral("trap")},
        {QStringLiteral("priority"), QStringLiteral("trap")},
        {QStringLiteral("trap-t"), QStringLiteral("invtrap")},
        {QStringLiteral("trapezoid-top"), QStringLiteral("invtrap")},
        {QStringLiteral("manual"), QStringLiteral("invtrap")},
        {QStringLiteral("odd"), QStringLiteral("odd")},
    };
    return shapes;
}

bool isOpener(QChar c)
{
    return c == QLatin1Char('[') || c == QLatin1Char('(') || c == QLatin1Char('{');
}

bool isCloser(QChar c)
{
    return c == QLatin1Char(']') || c == QLatin1Char(')') || c == QLatin1Char('}');
}

// Hides quoted spans behind placeholders, so that the structural scanning
// never looks inside a label: `A["a --> b"]` is one node.
QString maskQuotes(const QString &text, QStringList &quoted)
{
    static const QRegularExpression quotes(QStringLiteral("\"[^\"]*\""));
    QString masked;
    qsizetype last = 0;
    for (auto it = quotes.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        masked += text.mid(last, m.capturedStart() - last);
        masked += QChar(0xE000) + QString::number(quoted.size()) + QChar(0xE000);
        quoted.append(m.captured(0));
        last = m.capturedEnd();
    }
    return masked + text.mid(last);
}

QString unmask(const QString &text, const QStringList &quoted)
{
    static const QRegularExpression placeholder(QStringLiteral("\\x{E000}(\\d+)\\x{E000}"));
    QString result;
    qsizetype last = 0;
    for (auto it = placeholder.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        result += text.mid(last, m.capturedStart() - last);
        result += quoted.value(m.captured(1).toInt());
        last = m.capturedEnd();
    }
    return result + text.mid(last);
}

// Rewrites only what lies between node references, never inside a label.
QString outsideBrackets(const QString &stmt, const std::function<QString(const QString &)> &rewrite)
{
    QString out;
    int depth = 0;
    qsizetype start = 0;
    for (qsizetype i = 0; i < stmt.size(); ++i) {
        const QChar c = stmt[i];
        if (isOpener(c)) {
            if (depth == 0) {
                out += rewrite(stmt.mid(start, i - start));
                start = i;
            }
            ++depth;
        } else if (isCloser(c)) {
            depth = std::max(0, depth - 1);
            if (depth == 0) {
                out += stmt.mid(start, i + 1 - start);
                start = i + 1;
            }
        }
    }
    return out + (depth == 0 ? rewrite(stmt.mid(start)) : stmt.mid(start));
}

// [group, op, label, group, …], operators nested in node delimiters left
// where they are.
QStringList splitEdges(const QString &stmt)
{
    QStringList parts;
    int depth = 0;
    qsizetype start = 0;
    for (qsizetype i = 0; i < stmt.size(); ++i) {
        const QChar c = stmt[i];
        if (isOpener(c)) {
            ++depth;
        } else if (isCloser(c)) {
            depth = std::max(0, depth - 1);
        } else if (depth == 0) {
            const QRegularExpressionMatch m = edgeSplit().match(stmt, i, QRegularExpression::NormalMatch,
                                                                QRegularExpression::AnchorAtOffsetMatchOption);
            if (m.hasMatch()) {
                parts.append(stmt.mid(start, i - start));
                parts.append(m.captured(1));
                parts.append(m.captured(2));
                start = m.capturedEnd();
                i = start - 1;
            }
        }
    }
    parts.append(stmt.mid(start));
    return parts;
}

struct EdgeKind {
    QString line;
    QString head;
    bool reversed;
};

EdgeKind classifyEdge(const QString &op)
{
    const QString line = op.contains(QLatin1Char('~'))   ? QStringLiteral("invisible")
                         : op.contains(QLatin1Char('=')) ? QStringLiteral("thick")
                         : op.contains(QLatin1Char('.')) ? QStringLiteral("dotted")
                                                         : QStringLiteral("solid");
    const QChar last = op.isEmpty() ? QChar() : op.back();
    const QString head = last == QLatin1Char('>')   ? QStringLiteral("arrow")
                         : last == QLatin1Char('x') ? QStringLiteral("cross")
                         : last == QLatin1Char('o') ? QStringLiteral("circle")
                                                    : QStringLiteral("none");
    // A left head without a right one reverses the edge (A <-- B ≡ B --> A).
    const bool reversed = op.startsWith(QLatin1Char('<')) && head == QLatin1String("none");
    return {line, reversed ? QStringLiteral("arrow") : head, reversed};
}

struct Node {
    QString key;
    QString type = QStringLiteral("process");
    QString text;
    QString variant;
};

struct Nodes {
    QHash<QString, int> index;
    QList<Node> list;
};

// One node reference: optional id, optional delimited label, optional v11
// shape data.
Node *parseNodeRef(const QString &ref, Nodes &nodes, const QStringList &quoted)
{
    static const QRegularExpression shapeData(QStringLiteral("@\\{([^}]*)\\}"));
    static const QRegularExpression nodeRef(QStringLiteral("^\\s*(") + idPattern + QStringLiteral(")?\\s*([\\s\\S]*)$"),
                                            QRegularExpression::UseUnicodePropertiesOption);
    const QRegularExpressionMatch data = shapeData.match(ref);
    const QRegularExpressionMatch m = nodeRef.match(data.hasMatch() ? ref.left(data.capturedStart()) : ref);
    if (!m.hasMatch())
        return nullptr;
    QString key = m.captured(1);
    const QString rest = m.captured(2).trimmed();
    QString variant;
    std::optional<QString> label;
    bool delimited = false;
    if (!rest.isEmpty()) {
        for (const Delimiter &d : delimiters) {
            const QString open = QString::fromLatin1(d.open), close = QString::fromLatin1(d.close);
            if (rest.startsWith(open) && rest.endsWith(close) && rest.size() > open.size() + close.size() - 1) {
                variant = QString::fromLatin1(d.variant);
                label = mermaidUnquote(
                    unmask(rest.mid(open.size(), rest.size() - open.size() - close.size()).trimmed(), quoted));
                delimited = true;
                break;
            }
        }
        if (!delimited)
            return nullptr; // trailing garbage: not a node
    }
    if (data.hasMatch()) {
        for (const QString &entry : splitTop(unmask(data.captured(1), quoted), QLatin1Char(','))) {
            static const QRegularExpression pairPattern(QStringLiteral("^([\\w-]+)\\s*:\\s*([\\s\\S]*)$"));
            const QRegularExpressionMatch p = pairPattern.match(entry);
            if (!p.hasMatch())
                continue;
            QString value = p.captured(2).trimmed();
            if (p.captured(1) == QLatin1String("shape")) {
                static const QRegularExpression quotes(QStringLiteral("^[\"']|[\"']$"));
                value.remove(quotes);
                const QString mapped = v11Shapes().value(value);
                if (!mapped.isEmpty()) {
                    variant = mapped;
                    delimited = true;
                }
            } else if (p.captured(1) == QLatin1String("label") || p.captured(1) == QLatin1String("title")) {
                label = mermaidUnquote(value);
            }
        }
    }
    if (key.isEmpty() && !label)
        return nullptr;
    if (key.isEmpty())
        key = *label; // a labeled node without id: its label is its identity
    int at = nodes.index.value(key, -1);
    if (at < 0) {
        at = int(nodes.list.size());
        nodes.index.insert(key, at);
        nodes.list.append(Node{key, QStringLiteral("process"), key, {}});
    }
    Node &node = nodes.list[at];
    if (label)
        node.text = *label;
    if (label || delimited) {
        node.variant = variant;
        node.type = typeOf(variant);
    }
    return &node;
}

class MermaidFlow : public Codec {
public:
    QString id() const override { return QStringLiteral("mermaid-flow"); }

    QString language() const override { return QStringLiteral("mermaid"); }

    QString title() const override { return QStringLiteral("Flowchart"); }

    const Family &family() const override { return families::flowchart(); }

    bool matches(const QString &source) const override
    {
        QString start = source;
        static const QRegularExpression leading(QStringLiteral("^\\s+"));
        start.remove(leading);
        // Comments (the layout, a title) may come first.
        for (const QString &line : mermaidLines(start))
            return header().match(line).hasMatch();
        return false;
    }

    bool parse(const QString &source, Model &model, QString *) const override
    {
        Nodes nodes;
        QString rankdir = QStringLiteral("TB");
        QStringList statements;
        for (const QString &line : mermaidLines(source))
            statements += splitTop(line, QLatin1Char(';'));
        for (QString stmt : statements) {
            const QRegularExpressionMatch h = header().match(stmt);
            if (h.hasMatch()) {
                rankdir = h.captured(1).isEmpty() ? QStringLiteral("TB") : h.captured(1).toUpper();
                if (rankdir == QLatin1String("TD"))
                    rankdir = QStringLiteral("TB");
                continue;
            }
            if (skip().match(stmt).hasMatch())
                continue;
            // Labels go behind placeholders first: an operator written in a
            // label is only text.
            QStringList quoted;
            const QString masked = maskQuotes(stmt, quoted);
            stmt = outsideBrackets(masked, [](const QString &span) {
                static const QRegularExpression cssClass(QStringLiteral(":::[\\p{L}\\p{N}_-]+"),
                                                         QRegularExpression::UseUnicodePropertiesOption);
                static const QRegularExpression thick(QStringLiteral("==\\s*([^=|>]+?)\\s*==>"));
                static const QRegularExpression dotted(QStringLiteral("-\\.\\s*([^.|]+?)\\s*\\.->"));
                static const QRegularExpression solid(QStringLiteral("--\\s*([^-|>][^->]*?)\\s*-->"));
                QString result = span;
                result.remove(cssClass);
                result.replace(thick, QStringLiteral("==>|\\1|"));
                result.replace(dotted, QStringLiteral("-.->|\\1|"));
                result.replace(solid, QStringLiteral("-->|\\1|"));
                return result;
            });
            const QStringList parts = splitEdges(stmt);
            const auto refs = [&](const QString &group) {
                QList<QString> keys;
                for (const QString &ref : splitTop(group, QLatin1Char('&'))) {
                    if (Node *node = parseNodeRef(ref, nodes, quoted))
                        keys.append(node->key);
                }
                return keys;
            };
            QList<QString> previous = refs(parts.value(0));
            if (previous.isEmpty() && parts.size() == 1 && !stmt.trimmed().isEmpty())
                model.warnings.append(QStringLiteral("Not understood: %1").arg(unmask(stmt, quoted)));
            for (qsizetype i = 1; i + 2 < parts.size() + 1; i += 3) {
                const EdgeKind kind = classifyEdge(parts[i]);
                const QString label = parts.value(i + 1).isEmpty()
                                          ? QString()
                                          : mermaidUnquote(unmask(parts.value(i + 1).trimmed(), quoted));
                const QList<QString> next = refs(parts.value(i + 2));
                for (const QString &a : previous) {
                    for (const QString &b : next) {
                        const QString from = kind.reversed ? b : a;
                        const QString to = kind.reversed ? a : b;
                        model.links.append({QStringLiteral("flow"), from, to,
                                            QJsonObject{{QStringLiteral("label"), label},
                                                        {QStringLiteral("line"), kind.line},
                                                        {QStringLiteral("head"), kind.head}}});
                    }
                }
                previous = next;
            }
        }
        for (const Node &node : nodes.list) {
            QJsonObject props{{QStringLiteral("text"), node.text}};
            if (!node.variant.isEmpty())
                props.insert(QStringLiteral("variant"), node.variant);
            model.nodes.append({node.type, node.key, props});
        }
        model.meta = QJsonObject{{QStringLiteral("rankdir"), rankdir}};
        return true;
    }

    QString serialize(const Board &board, const QJsonObject &meta) const override
    {
        QSet<QString> used;
        QHash<QString, QString> keys;
        QStringList out{QStringLiteral("flowchart ") +
                        meta.value(QStringLiteral("rankdir")).toString(QStringLiteral("TB"))};
        for (const auto &s : board.shapes()) {
            const QString key = keyFor(s->key, s->displayText(), used);
            keys.insert(s->id, key);
            if (s->type == QLatin1String("flowText")) {
                out.append(QStringLiteral("  %1@{ shape: text, label: %2 }").arg(key, mermaidQuote(s->displayText())));
                continue;
            }
            const auto *flowShape = dynamic_cast<const FlowShape *>(s.get());
            const auto [open, close] = delimitersOf(flowShape ? flowShape->variant : QString(), s->type);
            out.append(QStringLiteral("  ") + key + open + mermaidQuote(s->displayText()) + close);
        }
        for (const auto &l : board.links()) {
            const QString from = keys.value(l->from), to = keys.value(l->to);
            if (from.isEmpty() || to.isEmpty())
                continue;
            const auto *flowLink = dynamic_cast<const FlowLink *>(l.get());
            const QString line = flowLink ? flowLink->line : QStringLiteral("solid");
            const QString head = flowLink ? flowLink->head : QStringLiteral("arrow");
            const bool none = head == QLatin1String("none");
            const QString op = line == QLatin1String("thick") ? (none ? QStringLiteral("===") : QStringLiteral("==>"))
                               : line == QLatin1String("dotted")
                                   ? (none ? QStringLiteral("-.-") : QStringLiteral("-.->"))
                               : line == QLatin1String("invisible") ? QStringLiteral("~~~")
                               : none                               ? QStringLiteral("---")
                               : head == QLatin1String("circle")    ? QStringLiteral("--o")
                               : head == QLatin1String("cross")     ? QStringLiteral("--x")
                                                                    : QStringLiteral("-->");
            const QString label =
                l->label.isEmpty() ? QString() : QLatin1Char('|') + pipeLabel(l->label) + QLatin1Char('|');
            out.append(QStringLiteral("  %1 %2%3 %4").arg(from, op, label, to));
        }
        return out.join(QLatin1Char('\n'));
    }

    QString blank() const override { return QStringLiteral("flowchart TB\n  A[First step] --> B[Next step]"); }
};

// Flowboard's .flow files: JSON with the coordinates in it.
class FlowboardJson : public Codec {
public:
    QString id() const override { return QStringLiteral("flowboard-json"); }

    QString language() const override { return QStringLiteral("flowboard"); }

    QString title() const override { return QStringLiteral("Flowchart"); }

    const Family &family() const override { return families::flowchart(); }

    bool sourceOwnsLayout() const override { return true; }

    bool matches(const QString &source) const override
    {
        const QString trimmed = source.trimmed();
        return trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('{'));
    }

    bool parse(const QString &source, Model &model, QString *error) const override
    {
        if (source.trimmed().isEmpty()) {
            model.meta = QJsonObject{{QStringLiteral("rankdir"), QStringLiteral("TB")}};
            return true;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(source.toUtf8(), &parseError);
        if (!document.isObject()) {
            if (error)
                *error = parseError.errorString();
            return false;
        }
        const QJsonObject root = document.object();
        static const QSet<QString> variants{
            QStringLiteral("square"),       QStringLiteral("round"),    QStringLiteral("stadium"),
            QStringLiteral("subroutine"),   QStringLiteral("cylinder"), QStringLiteral("circle"),
            QStringLiteral("doublecircle"), QStringLiteral("ellipse"),  QStringLiteral("hexagon"),
            QStringLiteral("diamond"),      QStringLiteral("leanr"),    QStringLiteral("leanl"),
            QStringLiteral("trap"),         QStringLiteral("invtrap"),  QStringLiteral("odd"),
            QStringLiteral("text")};
        QHash<QString, QString> keyOf; // id or key → node key
        QSet<QString> used;
        int index = 0;
        for (const QJsonValue &value : root.value(QStringLiteral("shapes")).toArray()) {
            const QJsonObject s = value.toObject();
            ++index;
            QString variant = s.value(QStringLiteral("variant")).toString(QStringLiteral("square"));
            if (!variants.contains(variant)) {
                model.warnings.append(QStringLiteral("Unknown variant \"%1\": drawn as a process").arg(variant));
                variant = QStringLiteral("square");
            }
            const QString id = s.value(QStringLiteral("id")).isDouble()
                                   ? QString::number(s.value(QStringLiteral("id")).toDouble())
                                   : s.value(QStringLiteral("id")).toString();
            QString key = s.value(QStringLiteral("key")).toString();
            if (key.isEmpty())
                key = id.isEmpty() ? QStringLiteral("n%1").arg(index) : QStringLiteral("n") + id;
            key = keyFor(key, QString(), used);
            if (!id.isEmpty()) {
                if (keyOf.contains(id))
                    model.warnings.append(QStringLiteral("Duplicate shape id %1").arg(id));
                keyOf.insert(id, key);
            }
            keyOf.insert(key, key);
            QJsonObject props{{QStringLiteral("text"), s.value(QStringLiteral("text")).toString()},
                              {QStringLiteral("variant"), variant}};
            for (const char *field : {"x", "y"}) {
                if (s.value(QLatin1String(field)).isDouble())
                    props.insert(QLatin1String(field), s.value(QLatin1String(field)));
            }
            // Flowboard writes a size only when it was chosen by hand.
            for (const char *field : {"w", "h"}) {
                if (s.value(QLatin1String(field)).isDouble())
                    props.insert(QLatin1String(field), s.value(QLatin1String(field)));
            }
            const QJsonValue fill = s.value(QStringLiteral("fill"));
            if (fill.isString() && isHexColor(fill.toString()))
                props.insert(QStringLiteral("customFill"), fill.toString());
            else if (fill.isDouble())
                props.insert(QStringLiteral("customFill"),
                             QStringLiteral("#%1").arg(qint64(fill.toDouble()), 6, 16, QLatin1Char('0')));
            model.nodes.append({typeOf(variant), key, props});
        }
        for (const QJsonValue &value : root.value(QStringLiteral("links")).toArray()) {
            const QJsonObject l = value.toObject();
            const auto end = [&](const char *name) {
                const QJsonValue v = l.value(QLatin1String(name));
                return keyOf.value(v.isDouble() ? QString::number(v.toDouble()) : v.toString());
            };
            const QString from = end("from"), to = end("to");
            if (from.isEmpty() || to.isEmpty()) {
                model.warnings.append(QStringLiteral("A link has a missing end"));
                continue;
            }
            QJsonObject props{
                {QStringLiteral("label"), l.value(QStringLiteral("label")).toString()},
                {QStringLiteral("line"), l.value(QStringLiteral("line")).toString(QStringLiteral("solid"))},
                {QStringLiteral("head"), l.value(QStringLiteral("head")).toString(QStringLiteral("arrow"))}};
            model.links.append({QStringLiteral("flow"), from, to, props});
        }
        model.meta = QJsonObject{
            {QStringLiteral("rankdir"), root.value(QStringLiteral("rankdir")).toString(QStringLiteral("TB"))}};
        return true;
    }

    QString serialize(const Board &board, const QJsonObject &meta) const override
    {
        QJsonArray shapes, links;
        QHash<QString, int> ids;
        int next = 1;
        for (const auto &s : board.shapes()) {
            ids.insert(s->id, next);
            const auto *flowShape = dynamic_cast<const FlowShape *>(s.get());
            QJsonObject json{
                {QStringLiteral("id"), next++},
                {QStringLiteral("variant"),
                 s->type == QLatin1String("flowText")
                     ? QStringLiteral("text")
                     : (flowShape && !flowShape->variant.isEmpty() ? flowShape->variant : QStringLiteral("square"))},
                {QStringLiteral("text"), s->displayText()},
                {QStringLiteral("x"), std::round(s->x * 10) / 10},
                {QStringLiteral("y"), std::round(s->y * 10) / 10}};
            if (!s->key.isEmpty())
                json.insert(QStringLiteral("key"), s->key);
            const bool manual = (s->manualWidth && std::abs(*s->manualWidth - s->resizeMinWidth) > 0.5) ||
                                (s->manualHeight && std::abs(*s->manualHeight - s->resizeMinHeight) > 0.5);
            if (manual) {
                json.insert(QStringLiteral("w"), std::round(s->w * 10) / 10);
                json.insert(QStringLiteral("h"), std::round(s->h * 10) / 10);
            }
            if (!s->customFill.isEmpty())
                json.insert(QStringLiteral("fill"), s->customFill);
            shapes.append(json);
        }
        for (const auto &l : board.links()) {
            const auto *flowLink = dynamic_cast<const FlowLink *>(l.get());
            QJsonObject json{{QStringLiteral("from"), ids.value(l->from)}, {QStringLiteral("to"), ids.value(l->to)}};
            if (!l->label.isEmpty())
                json.insert(QStringLiteral("label"), l->label);
            if (flowLink && flowLink->line != QLatin1String("solid"))
                json.insert(QStringLiteral("line"), flowLink->line);
            if (flowLink && flowLink->head != QLatin1String("arrow"))
                json.insert(QStringLiteral("head"), flowLink->head);
            links.append(json);
        }
        const QJsonObject root{
            {QStringLiteral("format"), QStringLiteral("flowboard")},
            {QStringLiteral("version"), 1},
            {QStringLiteral("rankdir"), meta.value(QStringLiteral("rankdir")).toString(QStringLiteral("TB"))},
            {QStringLiteral("shapes"), shapes},
            {QStringLiteral("links"), links}};
        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)).trimmed();
    }

    QString blank() const override
    {
        return QStringLiteral("{\n  \"format\": \"flowboard\",\n  \"version\": 1,\n  \"rankdir\": \"TB\",\n"
                              "  \"shapes\": [],\n  \"links\": []\n}");
    }
};

} // namespace

} // namespace diagram::flow

namespace diagram::families {

QList<const Codec *> flowchartCodecs()
{
    static const flow::MermaidFlow mermaid;
    static const flow::FlowboardJson flowboard;
    return {&mermaid, &flowboard};
}

} // namespace diagram::families
