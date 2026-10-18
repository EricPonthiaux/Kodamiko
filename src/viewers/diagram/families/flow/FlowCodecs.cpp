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

#include <array>
#include <cmath>
#include <functional>
#include <optional>

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

constexpr auto delimiters = std::to_array<Delimiter>({
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
});

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

QString lineOf(const QString &op)
{
    if (op.contains(QLatin1Char('~')))
        return QStringLiteral("invisible");
    if (op.contains(QLatin1Char('=')))
        return QStringLiteral("thick");
    if (op.contains(QLatin1Char('.')))
        return QStringLiteral("dotted");
    return QStringLiteral("solid");
}

// The head at the operator's right end.
QString headOf(const QString &op)
{
    const QChar last = op.isEmpty() ? QChar() : op.back();
    if (last == QLatin1Char('>'))
        return QStringLiteral("arrow");
    if (last == QLatin1Char('x'))
        return QStringLiteral("cross");
    if (last == QLatin1Char('o'))
        return QStringLiteral("circle");
    return QStringLiteral("none");
}

EdgeKind classifyEdge(const QString &op)
{
    const QString line = lineOf(op);
    const QString head = headOf(op);
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

// What a reference says of its node's look.
struct NodeLook {
    QString variant;
    std::optional<QString> label;
    // A shape was written, even without a label.
    bool delimited = false;
};

// The delimited label after a node's id; nothing when the rest is not one
// (trailing garbage: not a node).
std::optional<NodeLook> parseDelimited(const QString &rest, const QStringList &quoted)
{
    if (rest.isEmpty())
        return NodeLook{};
    for (const Delimiter &d : delimiters) {
        const QString open = QString::fromLatin1(d.open);
        const QString close = QString::fromLatin1(d.close);
        const bool isEnclosed =
            rest.startsWith(open) && rest.endsWith(close) && rest.size() > open.size() + close.size() - 1;
        if (!isEnclosed)
            continue;
        const QString inner = rest.mid(open.size(), rest.size() - open.size() - close.size()).trimmed();
        return NodeLook{QString::fromLatin1(d.variant), mermaidUnquote(unmask(inner, quoted)), true};
    }
    return std::nullopt;
}

// The variant of a v11 `shape:` value, or empty when it names none.
QString v11Variant(QString value)
{
    static const QRegularExpression quotes(QStringLiteral("^[\"']|[\"']$"));
    value.remove(quotes);
    return v11Shapes().value(value);
}

// v11 shape data (`@{ shape: …, label: … }`) overrides the delimiters.
void applyShapeData(const QString &data, NodeLook &look)
{
    static const QRegularExpression pairPattern(QStringLiteral("^([\\w-]+)\\s*:\\s*([\\s\\S]*)$"));
    for (const QString &entry : splitTop(data, QLatin1Char(','))) {
        const QRegularExpressionMatch p = pairPattern.match(entry);
        if (!p.hasMatch())
            continue;
        const QString field = p.captured(1);
        const QString value = p.captured(2).trimmed();
        const bool isLabel = field == QLatin1String("label") || field == QLatin1String("title");
        if (field == QLatin1String("shape")) {
            const QString mapped = v11Variant(value);
            if (!mapped.isEmpty()) {
                look.variant = mapped;
                look.delimited = true;
            }
        } else if (isLabel) {
            look.label = mermaidUnquote(value);
        }
    }
}

// The node keyed `key`, a new process labeled by its key when unknown.
Node &nodeFor(Nodes &nodes, const QString &key)
{
    int at = nodes.index.value(key, -1);
    if (at < 0) {
        at = int(nodes.list.size());
        nodes.index.insert(key, at);
        nodes.list.append(Node{key, QStringLiteral("process"), key, {}});
    }
    return nodes.list[at];
}

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
    std::optional<NodeLook> look = parseDelimited(m.captured(2).trimmed(), quoted);
    if (!look)
        return nullptr;
    if (data.hasMatch())
        applyShapeData(unmask(data.captured(1), quoted), *look);
    const bool hasId = !m.captured(1).isEmpty();
    if (!hasId && !look->label)
        return nullptr;
    // A labeled node without id: its label is its identity.
    const QString key = hasId ? m.captured(1) : look->label.value_or(QString());
    Node &node = nodeFor(nodes, key);
    if (look->label)
        node.text = *look->label;
    if (look->label || look->delimited) {
        node.variant = look->variant;
        node.type = typeOf(look->variant);
    }
    return &node;
}

// Turns the inline label forms (`-- a -->`, `== a ==>`, `-. a .->`) into
// pipe labels, and drops `:::class` decorations.
QString pipeInlineLabels(const QString &span)
{
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
}

// The keys of a `A & B` group.
QList<QString> parseGroup(const QString &group, Nodes &nodes, const QStringList &quoted)
{
    QList<QString> keys;
    for (const QString &ref : splitTop(group, QLatin1Char('&'))) {
        if (const Node *node = parseNodeRef(ref, nodes, quoted))
            keys.append(node->key);
    }
    return keys;
}

// Every link from a group to the next, `A & B --> C & D` making four.
QList<Model::Edge> edgesBetween(const EdgeKind &kind,
                                const QString &label,
                                const QList<QString> &sources,
                                const QList<QString> &targets)
{
    const QJsonObject props{
        {QStringLiteral("label"), label}, {QStringLiteral("line"), kind.line}, {QStringLiteral("head"), kind.head}};
    QList<Model::Edge> edges;
    for (const QString &a : sources) {
        for (const QString &b : targets) {
            const QString from = kind.reversed ? b : a;
            const QString to = kind.reversed ? a : b;
            edges.append({QStringLiteral("flow"), from, to, props});
        }
    }
    return edges;
}

// A statement of nodes and edges (neither the header nor a skipped one).
void parseStatement(const QString &statement, Nodes &nodes, Model &model)
{
    // Labels go behind placeholders first: an operator written in a label
    // is only text.
    QStringList quoted;
    const QString stmt = outsideBrackets(maskQuotes(statement, quoted), pipeInlineLabels);
    const QStringList parts = splitEdges(stmt);
    QList<QString> previous = parseGroup(parts.value(0), nodes, quoted);
    if (previous.isEmpty() && parts.size() == 1 && !stmt.trimmed().isEmpty())
        model.warnings.append(QStringLiteral("Not understood: %1").arg(unmask(stmt, quoted)));
    for (qsizetype i = 1; i + 2 < parts.size() + 1; i += 3) {
        const QString labelPart = parts.value(i + 1);
        const QString label = labelPart.isEmpty() ? QString() : mermaidUnquote(unmask(labelPart.trimmed(), quoted));
        QList<QString> next = parseGroup(parts.value(i + 2), nodes, quoted);
        model.links.append(edgesBetween(classifyEdge(parts[i]), label, previous, next));
        previous = std::move(next);
    }
}

void appendNodes(const Nodes &nodes, Model &model)
{
    for (const Node &node : nodes.list) {
        QJsonObject props{{QStringLiteral("text"), node.text}};
        if (!node.variant.isEmpty())
            props.insert(QStringLiteral("variant"), node.variant);
        model.nodes.append({node.type, node.key, props});
    }
}

// The Mermaid operator of a link's line and head.
QString edgeOperator(const QString &line, const QString &head)
{
    const bool none = head == QLatin1String("none");
    if (line == QLatin1String("thick"))
        return none ? QStringLiteral("===") : QStringLiteral("==>");
    if (line == QLatin1String("dotted"))
        return none ? QStringLiteral("-.-") : QStringLiteral("-.->");
    if (line == QLatin1String("invisible"))
        return QStringLiteral("~~~");
    if (none)
        return QStringLiteral("---");
    if (head == QLatin1String("circle"))
        return QStringLiteral("--o");
    if (head == QLatin1String("cross"))
        return QStringLiteral("--x");
    return QStringLiteral("-->");
}

QString mermaidNodeLine(const Shape &shape, const QString &key)
{
    if (shape.type == QLatin1String("flowText"))
        return QStringLiteral("  %1@{ shape: text, label: %2 }").arg(key, mermaidQuote(shape.displayText()));
    const auto *flowShape = dynamic_cast<const FlowShape *>(&shape);
    const auto [open, close] = delimitersOf(flowShape ? flowShape->variant : QString(), shape.type);
    return QStringLiteral("  ") + key + open + mermaidQuote(shape.displayText()) + close;
}

QString mermaidLinkLine(const Link &link, const QString &from, const QString &to)
{
    const auto *flowLink = dynamic_cast<const FlowLink *>(&link);
    const QString line = flowLink ? flowLink->line : QStringLiteral("solid");
    const QString head = flowLink ? flowLink->head : QStringLiteral("arrow");
    const QString label =
        link.label.isEmpty() ? QString() : QLatin1Char('|') + pipeLabel(link.label) + QLatin1Char('|');
    return QStringLiteral("  %1 %2%3 %4").arg(from, edgeOperator(line, head), label, to);
}

// The direction a header gives, `TD` being Graphviz's `TB`.
QString rankdirOf(const QRegularExpressionMatch &headerMatch)
{
    QString written = headerMatch.captured(1).toUpper();
    if (written.isEmpty() || written == QLatin1String("TD"))
        return QStringLiteral("TB");
    return written;
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
        for (const QString &stmt : statements) {
            const QRegularExpressionMatch h = header().match(stmt);
            if (h.hasMatch()) {
                rankdir = rankdirOf(h);
                continue;
            }
            if (!skip().match(stmt).hasMatch())
                parseStatement(stmt, nodes, model);
        }
        appendNodes(nodes, model);
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
            out.append(mermaidNodeLine(*s, key));
        }
        for (const auto &l : board.links()) {
            const QString from = keys.value(l->from);
            const QString to = keys.value(l->to);
            if (from.isEmpty() || to.isEmpty())
                continue;
            out.append(mermaidLinkLine(*l, from, to));
        }
        return out.join(QLatin1Char('\n'));
    }

    QString blank() const override { return QStringLiteral("flowchart TB\n  A[First step] --> B[Next step]"); }
};

// A Flowboard id, written as a number or a string.
QString flowboardId(const QJsonValue &value)
{
    return value.isDouble() ? QString::number(value.toDouble()) : value.toString();
}

QString knownVariant(const QJsonObject &shape, Model &model)
{
    static const QSet<QString> variants{
        QStringLiteral("square"),       QStringLiteral("round"),    QStringLiteral("stadium"),
        QStringLiteral("subroutine"),   QStringLiteral("cylinder"), QStringLiteral("circle"),
        QStringLiteral("doublecircle"), QStringLiteral("ellipse"),  QStringLiteral("hexagon"),
        QStringLiteral("diamond"),      QStringLiteral("leanr"),    QStringLiteral("leanl"),
        QStringLiteral("trap"),         QStringLiteral("invtrap"),  QStringLiteral("odd"),
        QStringLiteral("text")};
    QString variant = shape.value(QStringLiteral("variant")).toString(QStringLiteral("square"));
    if (variants.contains(variant))
        return variant;
    model.warnings.append(QStringLiteral("Unknown variant \"%1\": drawn as a process").arg(variant));
    return QStringLiteral("square");
}

QJsonObject flowboardShapeProps(const QJsonObject &shape, const QString &variant)
{
    QJsonObject props{{QStringLiteral("text"), shape.value(QStringLiteral("text")).toString()},
                      {QStringLiteral("variant"), variant}};
    // Flowboard writes a size (w, h) only when it was chosen by hand.
    for (const char *field : {"x", "y", "w", "h"}) {
        if (shape.value(QLatin1String(field)).isDouble())
            props.insert(QLatin1String(field), shape.value(QLatin1String(field)));
    }
    const QJsonValue fill = shape.value(QStringLiteral("fill"));
    if (fill.isString() && isHexColor(fill.toString()))
        props.insert(QStringLiteral("customFill"), fill.toString());
    else if (fill.isDouble())
        props.insert(QStringLiteral("customFill"),
                     QStringLiteral("#%1").arg(qint64(fill.toDouble()), 6, 16, QLatin1Char('0')));
    return props;
}

// Reads the shapes into the model; returns their keys by id and by key.
QHash<QString, QString> readFlowboardShapes(const QJsonArray &shapes, Model &model)
{
    QHash<QString, QString> keyOf; // id or key → node key
    QSet<QString> used;
    int index = 0;
    for (const QJsonValue &value : shapes) {
        const QJsonObject s = value.toObject();
        ++index;
        const QString variant = knownVariant(s, model);
        const QString id = flowboardId(s.value(QStringLiteral("id")));
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
        model.nodes.append({typeOf(variant), key, flowboardShapeProps(s, variant)});
    }
    return keyOf;
}

void readFlowboardLinks(const QJsonArray &links, const QHash<QString, QString> &keyOf, Model &model)
{
    for (const QJsonValue &value : links) {
        const QJsonObject l = value.toObject();
        const QString from = keyOf.value(flowboardId(l.value(QStringLiteral("from"))));
        const QString to = keyOf.value(flowboardId(l.value(QStringLiteral("to"))));
        if (from.isEmpty() || to.isEmpty()) {
            model.warnings.append(QStringLiteral("A link has a missing end"));
            continue;
        }
        const QJsonObject props{
            {QStringLiteral("label"), l.value(QStringLiteral("label")).toString()},
            {QStringLiteral("line"), l.value(QStringLiteral("line")).toString(QStringLiteral("solid"))},
            {QStringLiteral("head"), l.value(QStringLiteral("head")).toString(QStringLiteral("arrow"))}};
        model.links.append({QStringLiteral("flow"), from, to, props});
    }
}

QString flowboardVariant(const Shape &shape)
{
    if (shape.type == QLatin1String("flowText"))
        return QStringLiteral("text");
    const auto *flowShape = dynamic_cast<const FlowShape *>(&shape);
    return flowShape && !flowShape->variant.isEmpty() ? flowShape->variant : QStringLiteral("square");
}

// A size differing from the minimum the text needs was chosen by hand.
bool hasManualSize(const Shape &shape)
{
    const bool manualWidth = shape.manualWidth && std::abs(*shape.manualWidth - shape.resizeMinWidth) > 0.5;
    const bool manualHeight = shape.manualHeight && std::abs(*shape.manualHeight - shape.resizeMinHeight) > 0.5;
    return manualWidth || manualHeight;
}

QJsonObject flowboardShape(const Shape &shape, int id)
{
    QJsonObject json{{QStringLiteral("id"), id},
                     {QStringLiteral("variant"), flowboardVariant(shape)},
                     {QStringLiteral("text"), shape.displayText()},
                     {QStringLiteral("x"), std::round(shape.x * 10) / 10},
                     {QStringLiteral("y"), std::round(shape.y * 10) / 10}};
    if (!shape.key.isEmpty())
        json.insert(QStringLiteral("key"), shape.key);
    if (hasManualSize(shape)) {
        json.insert(QStringLiteral("w"), std::round(shape.w * 10) / 10);
        json.insert(QStringLiteral("h"), std::round(shape.h * 10) / 10);
    }
    if (!shape.customFill.isEmpty())
        json.insert(QStringLiteral("fill"), shape.customFill);
    return json;
}

QJsonObject flowboardLink(const Link &link, const QHash<QString, int> &ids)
{
    const auto *flowLink = dynamic_cast<const FlowLink *>(&link);
    QJsonObject json{{QStringLiteral("from"), ids.value(link.from)}, {QStringLiteral("to"), ids.value(link.to)}};
    if (!link.label.isEmpty())
        json.insert(QStringLiteral("label"), link.label);
    if (flowLink && flowLink->line != QLatin1String("solid"))
        json.insert(QStringLiteral("line"), flowLink->line);
    if (flowLink && flowLink->head != QLatin1String("arrow"))
        json.insert(QStringLiteral("head"), flowLink->head);
    return json;
}

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
        const QHash<QString, QString> keyOf =
            readFlowboardShapes(root.value(QStringLiteral("shapes")).toArray(), model);
        readFlowboardLinks(root.value(QStringLiteral("links")).toArray(), keyOf, model);
        model.meta = QJsonObject{
            {QStringLiteral("rankdir"), root.value(QStringLiteral("rankdir")).toString(QStringLiteral("TB"))}};
        return true;
    }

    QString serialize(const Board &board, const QJsonObject &meta) const override
    {
        QJsonArray shapes;
        QJsonArray links;
        QHash<QString, int> ids;
        int next = 1;
        for (const auto &s : board.shapes()) {
            ids.insert(s->id, next);
            shapes.append(flowboardShape(*s, next++));
        }
        for (const auto &l : board.links())
            links.append(flowboardLink(*l, ids));
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
