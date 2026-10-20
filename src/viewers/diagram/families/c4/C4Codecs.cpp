// SPDX-License-Identifier: GPL-3.0-only

// Mermaid C4 (sidecar's fence/mermaid-c4.js): the core official vocabulary —
// people, systems, containers, components, databases, queues, relations and
// nested enterprise / system / container / deployment boundaries.

#include "C4.h"
#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/SourceText.h"
#include "viewers/diagram/families/Families.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <optional>
#include <vector>

namespace diagram::c4 {

namespace {

using namespace diagram::source;

const QRegularExpression &header()
{
    static const QRegularExpression pattern(
        QStringLiteral("^(C4(?:Context|Container|Component|Dynamic|Deployment))\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// A macro call: `Name(args)`, optionally opening a block.
const QRegularExpression &call()
{
    static const QRegularExpression pattern(QStringLiteral("^(\\w+)\\s*\\((.*)\\)\\s*(\\{)?\\s*$"));
    return pattern;
}

const QRegularExpression &relations()
{
    static const QRegularExpression pattern(
        QStringLiteral("^(?:BiRel|Rel|RelIndex)(?:_(?:U|Up|D|Down|L|Left|R|Right|Back))?$"));
    return pattern;
}

// Statements that only style or lay out the rendering: read, not reported.
const QRegularExpression &presentation()
{
    static const QRegularExpression pattern(
        QStringLiteral("^(?:Update(?:Element|Rel|Boundary)Style|UpdateLayoutConfig|Add(?:Element|Rel|Boundary)Tag)$"));
    return pattern;
}

bool isBoundary(const QString &macro)
{
    static const QSet<QString> boundaries{
        QStringLiteral("Boundary"),        QStringLiteral("Enterprise_Boundary"),
        QStringLiteral("System_Boundary"), QStringLiteral("Container_Boundary"),
        QStringLiteral("Deployment_Node"), QStringLiteral("Node"),
        QStringLiteral("Node_L"),          QStringLiteral("Node_R"),
    };
    return boundaries.contains(macro);
}

// A named argument (`$sprite="x"`) keeps only its value.
QString cleanArg(const QString &value)
{
    static const QRegularExpression named(QStringLiteral("^\\$\\w+\\s*=\\s*"));
    QString result = value.trimmed();
    result.remove(named);
    return result;
}

// The arguments of a call: split on commas outside quotes, quotes removed.
QStringList argsOf(const QString &text)
{
    QStringList out;
    QString value;
    bool quoted = false;
    for (const QChar c : text) {
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
            continue;
        }
        if (c == QLatin1Char(',') && !quoted) {
            out.append(cleanArg(value));
            value.clear();
            continue;
        }
        value += c;
    }
    out.append(cleanArg(value));
    return out;
}

struct Element {
    QString kind;
    QString variant;
    bool external = false;
};

// The silhouette a macro's suffix names: `SystemDb` → "database".
QString variantOf(const QString &macro)
{
    if (macro.endsWith(QLatin1String("Db")))
        return QStringLiteral("database");
    if (macro.endsWith(QLatin1String("Queue")))
        return QStringLiteral("queue");
    return QStringLiteral("box");
}

// `ContainerDb_Ext` → a container, database variant, external.
std::optional<Element> elementOf(QString macro)
{
    Element element;
    element.external = macro.endsWith(QLatin1String("_Ext"));
    if (element.external)
        macro.chop(4);
    element.variant = variantOf(macro);
    if (element.variant == QLatin1String("database"))
        macro.chop(2);
    else if (element.variant == QLatin1String("queue"))
        macro.chop(5);
    static const QHash<QString, QString> kinds{{QStringLiteral("Person"), QStringLiteral("person")},
                                               {QStringLiteral("System"), QStringLiteral("system")},
                                               {QStringLiteral("Container"), QStringLiteral("container")},
                                               {QStringLiteral("Component"), QStringLiteral("component")}};
    element.kind = kinds.value(macro);
    if (element.kind.isEmpty())
        return std::nullopt;
    return element;
}

bool isStructural(const QString &kind)
{
    return kind == QLatin1String("container") || kind == QLatin1String("component");
}

// Mermaid C4 arguments have no escape for a double quote.
QString quote(const QString &value)
{
    QString text = value;
    text.replace(QLatin1Char('"'), QLatin1Char('\''));
    return QLatin1Char('"') + text + QLatin1Char('"');
}

QString notUnderstood(const QString &line)
{
    return QStringLiteral("Not understood: %1").arg(line);
}

// One read of a Mermaid C4 source, line by line, into a model.
class Parser {
public:
    explicit Parser(Model &model)
        : model_(model)
    {
    }

    void read(const QString &line)
    {
        if (readHeading(line))
            return;
        if (line == QLatin1String("}")) {
            if (!boundaries_.isEmpty())
                boundaries_.removeLast();
            return;
        }
        static const QRegularExpression accessibility(QStringLiteral("^acc(?:Title|Descr)\\b"));
        if (accessibility.match(line).hasMatch())
            return;
        const QRegularExpressionMatch c = call().match(line);
        if (!c.hasMatch()) {
            model_.warnings.append(notUnderstood(line));
            return;
        }
        readCall(line, c.captured(1), argsOf(c.captured(2)), !c.captured(3).isEmpty());
    }

    void finish()
    {
        for (const Model::Edge &edge : model_.links) {
            for (const QString &end : {edge.from, edge.to}) {
                if (!indexOf_.contains(end))
                    model_.warnings.append(QStringLiteral("Relation to an undeclared element: %1").arg(end));
            }
        }
        model_.meta = QJsonObject{{QStringLiteral("rankdir"), QStringLiteral("LR")},
                                  {QStringLiteral("family"), family_},
                                  {QStringLiteral("title"), heading_}};
    }

private:
    // The diagram kind and its title.
    bool readHeading(const QString &line)
    {
        if (const QRegularExpressionMatch h = header().match(line); h.hasMatch()) {
            family_ = h.captured(1);
            return true;
        }
        static const QRegularExpression titleLine(QStringLiteral("^title\\s+(.+)$"),
                                                  QRegularExpression::CaseInsensitiveOption);
        if (const QRegularExpressionMatch t = titleLine.match(line); t.hasMatch()) {
            heading_ = t.captured(1).trimmed();
            return true;
        }
        return false;
    }

    void readCall(const QString &line, const QString &macro, const QStringList &args, bool opens)
    {
        if (isBoundary(macro) && !args.value(0).isEmpty()) {
            readBoundary(macro, args, opens);
            return;
        }
        if (relations().match(macro).hasMatch()) {
            readRelation(line, macro, args);
            return;
        }
        const std::optional<Element> element = elementOf(macro);
        if (!element || args.value(0).isEmpty()) {
            if (!presentation().match(macro).hasMatch())
                model_.warnings.append(notUnderstood(line));
            return;
        }
        readElement(*element, args);
    }

    void readBoundary(const QString &macro, const QStringList &args, bool opens)
    {
        const QString &key = args[0];
        const QString boundaryKind =
            macro.startsWith(QLatin1String("Node")) ? QStringLiteral("Deployment_Node") : macro;
        const QJsonObject props{{QStringLiteral("name"), args.value(1).isEmpty() ? key : args[1]},
                                {QStringLiteral("boundaryKind"), boundaryKind}};
        setNode(QStringLiteral("c4boundary"), key, withParent(props));
        if (opens)
            boundaries_.append(key);
    }

    void readRelation(const QString &line, const QString &macro, const QStringList &args)
    {
        const int offset = macro.startsWith(QLatin1String("RelIndex")) ? 1 : 0;
        if (args.size() < offset + 2) {
            model_.warnings.append(notUnderstood(line));
            return;
        }
        model_.links.append({QStringLiteral("c4relation"), args[offset], args[offset + 1],
                             QJsonObject{{QStringLiteral("label"), args.value(offset + 2)},
                                         {QStringLiteral("technology"), args.value(offset + 3)},
                                         {QStringLiteral("description"), args.value(offset + 4)},
                                         {QStringLiteral("bidirectional"), macro.startsWith(QLatin1String("BiRel"))}}});
    }

    void readElement(const Element &element, const QStringList &args)
    {
        // Containers and components carry a technology before their
        // description; people and systems do not.
        const bool structural = isStructural(element.kind);
        const QJsonObject props{{QStringLiteral("name"), args.value(1).isEmpty() ? args[0] : args[1]},
                                {QStringLiteral("kind"), element.kind},
                                {QStringLiteral("variant"), element.variant},
                                {QStringLiteral("external"), element.external},
                                {QStringLiteral("description"), structural ? args.value(3) : args.value(2)},
                                {QStringLiteral("technology"), structural ? args.value(2) : QString()}};
        setNode(QStringLiteral("c4"), args[0], withParent(props));
    }

    // The props of a node declared inside a boundary block name it.
    QJsonObject withParent(QJsonObject props) const
    {
        if (!boundaries_.isEmpty())
            props.insert(QStringLiteral("parentBoundary"), boundaries_.last());
        return props;
    }

    // A key declared twice is one element, the last declaration winning, at
    // the place of the first.
    void setNode(const QString &nodeType, const QString &key, const QJsonObject &props)
    {
        if (const auto it = indexOf_.constFind(key); it != indexOf_.constEnd()) {
            model_.nodes[*it] = {nodeType, key, props};
            return;
        }
        indexOf_.insert(key, model_.nodes.size());
        model_.nodes.append({nodeType, key, props});
    }

    Model &model_;
    QHash<QString, qsizetype> indexOf_;
    QStringList boundaries_;
    QString family_ = QStringLiteral("C4Context");
    QString heading_;
};

// `ContainerDb_Ext`: the macro that declares an element.
QString elementMacro(const C4ElementShape &element)
{
    static const QHash<QString, QString> bases{{QStringLiteral("person"), QStringLiteral("Person")},
                                               {QStringLiteral("system"), QStringLiteral("System")},
                                               {QStringLiteral("container"), QStringLiteral("Container")},
                                               {QStringLiteral("component"), QStringLiteral("Component")}};
    QString macro = bases.value(element.kind, QStringLiteral("System"));
    if (element.variant == QLatin1String("database"))
        macro += QStringLiteral("Db");
    else if (element.variant == QLatin1String("queue"))
        macro += QStringLiteral("Queue");
    if (element.external)
        macro += QStringLiteral("_Ext");
    return macro;
}

QString relationLine(const Link &link, const QString &from, const QString &to)
{
    const auto *relation = dynamic_cast<const C4RelationLink *>(&link);
    QStringList args{from, to, quote(link.label)};
    const QString technology = relation ? relation->technology : QString();
    const QString description = relation ? relation->description : QString();
    // The arguments are positional: a description needs the technology
    // before it, even empty.
    if (!technology.isEmpty() || !description.isEmpty())
        args.append(quote(technology));
    if (!description.isEmpty())
        args.append(quote(description));
    const bool bidirectional = relation && relation->bidirectional;
    return QStringLiteral("  %1(%2)")
        .arg(bidirectional ? QStringLiteral("BiRel") : QStringLiteral("Rel"), args.join(QStringLiteral(", ")));
}

// Writes the shapes of a board as nested boundaries and elements.
class ShapeWriter {
public:
    ShapeWriter(const Board &board, QStringList &out)
        : board_(board)
        , out_(out)
    {
        QSet<QString> used;
        for (const auto &s : board.shapes()) {
            shapes_.push_back(s.get());
            keys_.insert(s->id, keyFor(s->key, s->displayText(), used));
        }
    }

    const QHash<QString, QString> &keys() const { return keys_; }

    void writeAll()
    {
        for (const Shape *shape : shapes_) {
            if (parentKey(shape).isEmpty())
                write(shape, 1);
        }
        // Boundaries whose parenthood loops have no root: written at the top
        // rather than lost.
        for (const Shape *shape : shapes_)
            write(shape, 1);
    }

private:
    // The key of a shape's boundary; empty at the top level, and for a
    // parent that is gone (the shape then stays written, at the top).
    QString parentKey(const Shape *shape) const
    {
        if (shape->parentBoundary.isEmpty())
            return {};
        const Shape *parent = board_.shapeByReference(shape->parentBoundary);
        const bool isBoundaryParent = parent && parent != shape && parent->type == QLatin1String("c4boundary");
        return isBoundaryParent ? keys_.value(parent->id) : QString();
    }

    void write(const Shape *shape, int depth)
    {
        if (emitted_.contains(shape))
            return;
        emitted_.insert(shape);
        const QString indent(qsizetype(depth) * 2, QLatin1Char(' '));
        const QString key = keys_.value(shape->id);
        if (const auto *boundary = dynamic_cast<const C4BoundaryShape *>(shape)) {
            writeBoundary(*boundary, key, depth);
            return;
        }
        const auto *element = dynamic_cast<const C4ElementShape *>(shape);
        if (!element)
            return;
        QStringList args{key, quote(element->name)};
        if (isStructural(element->kind))
            args.append(quote(element->technology));
        args.append(quote(element->description));
        out_.append(indent + elementMacro(*element) + QLatin1Char('(') + args.join(QStringLiteral(", ")) +
                    QLatin1Char(')'));
    }

    void writeBoundary(const C4BoundaryShape &boundary, const QString &key, int depth)
    {
        const QString indent(qsizetype(depth) * 2, QLatin1Char(' '));
        const QString macro =
            boundary.boundaryKind.isEmpty() ? QStringLiteral("System_Boundary") : boundary.boundaryKind;
        out_.append(QStringLiteral("%1%2(%3, %4) {").arg(indent, macro, key, quote(boundary.name)));
        for (const Shape *child : shapes_) {
            if (parentKey(child) == key)
                write(child, depth + 1);
        }
        out_.append(indent + QLatin1Char('}'));
    }

    const Board &board_;
    QStringList &out_;
    std::vector<const Shape *> shapes_;
    QHash<QString, QString> keys_;
    QSet<const Shape *> emitted_;
};

class MermaidC4 : public Codec {
public:
    QString id() const override { return QStringLiteral("mermaid-c4"); }

    QString language() const override { return QStringLiteral("mermaid"); }

    QString title() const override { return QStringLiteral("C4 architecture"); }

    const Family &family() const override { return families::c4(); }

    bool matches(const QString &source) const override
    {
        // Comments (the layout, a title) may come first.
        for (const QString &line : mermaidLines(source))
            return header().match(line).hasMatch();
        return false;
    }

    bool parse(const QString &source, Model &model, QString *) const override
    {
        Parser parser(model);
        for (const QString &line : mermaidLines(source))
            parser.read(line);
        parser.finish();
        return true;
    }

    QString serialize(const Board &board, const QJsonObject &meta) const override
    {
        const QString family = meta.value(QStringLiteral("family")).toString();
        QStringList out{family.isEmpty() ? QStringLiteral("C4Context") : family};
        const QString heading = meta.value(QStringLiteral("title")).toString();
        if (!heading.isEmpty())
            out.append(QStringLiteral("  title ") + heading);

        ShapeWriter shapes(board, out);
        shapes.writeAll();
        for (const auto &l : board.links()) {
            const QString from = shapes.keys().value(l->from);
            const QString to = shapes.keys().value(l->to);
            if (from.isEmpty() || to.isEmpty())
                continue;
            out.append(relationLine(*l, from, to));
        }
        return out.join(QLatin1Char('\n'));
    }

    QString blank() const override
    {
        return QStringLiteral("C4Context\n  title New architecture\n  Person(user, \"User\", \"Uses the system\")\n"
                              "  System(system, \"System\", \"Provides the service\")\n  Rel(user, system, \"Uses\")");
    }
};

} // namespace

} // namespace diagram::c4

namespace diagram::families {

QList<const Codec *> c4Codecs()
{
    static const c4::MermaidC4 mermaid;
    return {&mermaid};
}

} // namespace diagram::families
