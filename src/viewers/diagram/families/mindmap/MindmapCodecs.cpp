// SPDX-License-Identifier: GPL-3.0-only

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

#include <array>
#include <functional>
#include <utility>

namespace diagram::mindmap {

namespace {

using namespace diagram::source;

// Mindmap delimiters, longest first.
constexpr std::array<std::pair<const char *, const char *>, 7> delimiters{{
    {"((", "))"},
    {"))", "(("},
    {"{{", "}}"},
    {")", "("},
    {"([", "])"},
    {"[", "]"},
    {"(", ")"},
}};

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
        const QString open = QString::fromLatin1(openText);
        const QString close = QString::fromLatin1(closeText);
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

struct Entry {
    int indent;
    NodeText node;
};

// The leading-space count of a statement: its place in the hierarchy.
int indentOf(const QString &stmt)
{
    int indent = 0;
    while (indent < stmt.size() && stmt.at(indent).isSpace())
        ++indent;
    return indent;
}

// The source's nodes with their indent, decorations and blank lines left out.
QList<Entry> readEntries(const QString &source)
{
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
        const NodeText node = parseText(stmt);
        if (node.text.isEmpty())
            continue;
        entries.append({indentOf(stmt), node});
    }
    return entries;
}

QSet<QString> sourceIds(const QList<Entry> &entries)
{
    QSet<QString> ids;
    for (const Entry &entry : entries) {
        if (!entry.node.key.isEmpty())
            ids.insert(entry.node.key);
    }
    return ids;
}

// The shapes in writing order with their depth. A mind map has one root: the
// others are written as top-level branches so that no content is lost, then
// whatever only a cycle reaches.
QList<std::pair<QString, int>> writingOrder(const Board &board)
{
    QHash<QString, QList<QString>> children; // shape id → child ids
    QSet<QString> hasParent;
    for (const auto &l : board.links()) {
        children[l->from].append(l->to);
        hasParent.insert(l->to);
    }
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
    return order;
}

// A node's line: the root as a circle, a node keyed by its text's slug as a
// bare text (Mermaid's usual form), any other one with its id.
QString nodeLine(const QString &key, const QString &text, int depth)
{
    static const QRegularExpression special(QStringLiteral("[()\\[\\]{}\"\\n]"));
    // Also quoted: what a bare line would read differently (spaces around
    // it, the header's word).
    const bool needsQuotes = text.contains(special) || text != text.trimmed() ||
                             text.compare(QStringLiteral("mindmap"), Qt::CaseInsensitive) == 0;
    const QString inner = needsQuotes ? mermaidQuote(text) : text;
    const QString indent(qsizetype(depth + 1) * 2, QLatin1Char(' '));
    if (depth == 0)
        return indent + key + QStringLiteral("((") + inner + QStringLiteral("))");
    if (!needsQuotes && key == slugOf(text))
        return indent + text;
    return indent + key + QLatin1Char('[') + inner + QLatin1Char(']');
}

} // namespace

void parseTree(const QString &source, Model &model)
{
    const QList<Entry> entries = readEntries(source);
    // Keys: the source id when there is one, else a slug of the text (sidecar
    // numbers them m1, m2… in order; a slug keeps a node's place when a
    // branch is inserted before it). Source ids are reserved first so that a
    // slug never takes one.
    QSet<QString> used = sourceIds(entries);
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
        // Keys first, in the order written, so that the reading gives them
        // back.
        QSet<QString> used;
        for (const auto &[id, depth] : writingOrder(board)) {
            const Shape *s = board.shape(id);
            const QString text = s->displayText().trimmed().isEmpty() ? QStringLiteral("Idea") : s->displayText();
            out.append(nodeLine(keyFor(s->key, text, used), text, depth));
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
