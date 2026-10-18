// Mermaid mindmaps (sidecar's fence/mermaid-mindmap.js), drawn with the mind
// map family. Grammar checked against Mermaid's mindmap.jison: the hierarchy
// is the leading-space count of each line; node shapes are id((text)),
// id[text], id(text)…; `::icon(…)` and `:::class` decorations are ignored.

#include "Mindmap.h"
#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/SourceText.h"
#include "viewers/diagram/families/Families.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace diagram::mindmap {

namespace {

using namespace diagram::source;

// Mindmap delimiters, longest first.
const std::pair<const char *, const char *> delimiters[] = {
    {"((", "))"}, {"))", "(("}, {"{{", "}}"}, {")", "("}, {"([", "])"}, {"[", "]"}, {"(", ")"},
};

struct NodeText {
    QString key; // empty: a bare text
    QString text;
};

NodeText parseText(const QString &body)
{
    const QString trimmed = body.trimmed();
    static const QRegularExpression split(QStringLiteral("^([A-Za-z0-9_.-]*)\\s*(.*)$"),
                                          QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch m = split.match(trimmed);
    const QString rest = m.captured(2).trimmed();
    for (const auto &[openText, closeText] : delimiters) {
        const QString open = QString::fromLatin1(openText), close = QString::fromLatin1(closeText);
        if (rest.startsWith(open) && rest.endsWith(close) && rest.size() > open.size() + close.size() - 1) {
            QString text = rest.mid(open.size(), rest.size() - open.size() - close.size()).trimmed();
            if (text.size() >= 2 && text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"')))
                text = text.mid(1, text.size() - 2);
            return {m.captured(1), decodeLabel(text)};
        }
    }
    static const QRegularExpression quotes(QStringLiteral("^\"|\"$"));
    QString bare = trimmed;
    bare.remove(quotes);
    return {QString(), decodeLabel(bare)};
}

// The key a bare text gets when the source is read: its slug.
QString slugOf(const QString &text)
{
    QSet<QString> none;
    return keyFor(QString(), text, none);
}

// The first statement of the source, comments left out.
QString header(const QString &source)
{
    const QStringList lines = mermaidLines(source);
    return lines.isEmpty() ? QString() : lines.first();
}

} // namespace

void parseTree(const QString &source, Model &model)
{
    struct Entry {
        int indent;
        NodeText node;
    };

    static const QRegularExpression icon(QStringLiteral("::icon\\([^)]*\\)"));
    static const QRegularExpression cssClass(QStringLiteral(":::.*$"));
    static const QRegularExpression headerLine(QStringLiteral("^\\s*mindmap\\s*$"));
    QList<Entry> entries;
    for (QString stmt : mermaidLines(source, true)) {
        stmt.remove(icon);
        stmt.remove(cssClass);
        while (!stmt.isEmpty() && stmt.back().isSpace())
            stmt.chop(1);
        if (stmt.trimmed().isEmpty() || headerLine.match(stmt).hasMatch())
            continue;
        int indent = 0;
        while (indent < stmt.size() && stmt.at(indent).isSpace())
            ++indent;
        const NodeText node = parseText(stmt);
        if (node.text.isEmpty())
            continue;
        entries.append({indent, node});
    }

    // Keys: the source id when there is one, else a slug of the text (sidecar
    // numbers them m1, m2… in order; a slug keeps a node's place when a
    // branch is inserted before it). Source ids are reserved first so that a
    // slug never takes one.
    QSet<QString> used;
    for (const Entry &entry : entries) {
        if (!entry.node.key.isEmpty())
            used.insert(entry.node.key);
    }
    QSet<QString> assigned;

    struct Open {
        int indent;
        QString key;
    };

    QList<Open> stack; // the path to the current branch
    for (const Entry &entry : entries) {
        QString key;
        if (!entry.node.key.isEmpty() && !assigned.contains(entry.node.key))
            key = entry.node.key;
        else
            key = keyFor(entry.node.key, entry.node.text, used);
        assigned.insert(key);
        while (!stack.isEmpty() && stack.last().indent >= entry.indent)
            stack.removeLast();
        // The depth drives the presentation: 0 the root pill, 1 bubbles, 2+
        // bare leaves.
        model.nodes.append(
            {QStringLiteral("mind"), key,
             QJsonObject{{QStringLiteral("text"), entry.node.text}, {QStringLiteral("depth"), int(stack.size())}}});
        if (!stack.isEmpty())
            model.links.append({QStringLiteral("mind"), stack.last().key, key, QJsonObject{}});
        stack.append({entry.indent, key});
    }
}

namespace {

class MermaidMindmap : public Codec {
public:
    QString id() const override { return QStringLiteral("mermaid-mindmap"); }

    QString language() const override { return QStringLiteral("mermaid"); }

    QString title() const override { return QStringLiteral("Mind map"); }

    const Family &family() const override { return families::mindmap(); }

    bool matches(const QString &source) const override
    {
        static const QRegularExpression pattern(QStringLiteral("^mindmap\\b"));
        return pattern.match(header(source)).hasMatch();
    }

    bool parse(const QString &source, Model &model, QString *) const override
    {
        parseTree(source, model);
        // twopi: a radial tree around the root, Mermaid's exploded mind map
        // look. Graphviz takes as its root the first node nobody points to.
        model.meta = QJsonObject{{QStringLiteral("engine"), QStringLiteral("twopi")}};
        return true;
    }

    QString serialize(const Board &board, const QJsonObject &) const override
    {
        QStringList out{QStringLiteral("mindmap")};
        QHash<QString, QList<QString>> children; // shape id → child ids
        QSet<QString> hasParent;
        for (const auto &l : board.links()) {
            children[l->from].append(l->to);
            hasParent.insert(l->to);
        }

        // Roots: shapes nobody points to. A mind map has one root: the others
        // are written as top-level branches so that no content is lost, then
        // whatever only a cycle reaches.
        QList<std::pair<QString, int>> order; // shape id, depth
        QSet<QString> visited;
        const std::function<void(const QString &, int)> visit = [&](const QString &id, int depth) {
            if (visited.contains(id) || !board.shape(id))
                return; // a cycle cannot be expressed
            visited.insert(id);
            order.append({id, depth});
            for (const QString &child : children.value(id))
                visit(child, depth + 1);
        };
        bool first = true;
        for (const auto &s : board.shapes()) {
            if (!hasParent.contains(s->id)) {
                visit(s->id, first ? 0 : 1);
                first = false;
            }
        }
        for (const auto &s : board.shapes())
            visit(s->id, 1);

        // Keys first, in the order written, so that the reading gives them
        // back: a node whose key is its text's slug is written as a bare text
        // (Mermaid's usual form), any other one with its id.
        QSet<QString> used;
        for (const auto &[id, depth] : order) {
            const Shape *s = board.shape(id);
            const QString text = s->displayText().trimmed().isEmpty() ? QStringLiteral("Idea") : s->displayText();
            const QString key = keyFor(s->key, text, used);
            static const QRegularExpression special(QStringLiteral("[()\\[\\]{}\"\\n]"));
            // Also quoted: what a bare line would read differently (spaces
            // around it, the header's word).
            const bool needsQuotes = text.contains(special) || text != text.trimmed() ||
                                     text.compare(QStringLiteral("mindmap"), Qt::CaseInsensitive) == 0;
            const QString inner = needsQuotes ? mermaidQuote(text) : text;
            const QString indent(2 * (depth + 1), QLatin1Char(' '));
            if (depth == 0)
                out.append(indent + key + QStringLiteral("((") + inner + QStringLiteral("))"));
            else if (!needsQuotes && key == slugOf(text))
                out.append(indent + text);
            else
                out.append(indent + key + QLatin1Char('[') + inner + QLatin1Char(']'));
        }
        return out.join(QLatin1Char('\n'));
    }

    QString blank() const override
    {
        return QStringLiteral("mindmap\n  root((Central idea))\n    First branch\n      First path\n    Second branch");
    }
};

} // namespace

} // namespace diagram::mindmap

namespace diagram::families {

QList<const Codec *> mindmapCodecs()
{
    static const mindmap::MermaidMindmap mermaid;
    return {&mermaid};
}

} // namespace diagram::families
