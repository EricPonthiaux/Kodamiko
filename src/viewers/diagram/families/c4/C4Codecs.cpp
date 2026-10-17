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

#include <functional>

namespace diagram::c4 {

namespace {

using namespace diagram::source;

const QRegularExpression &header()
{
    static const QRegularExpression pattern(QStringLiteral("^(C4(?:Context|Container|Component|Dynamic|Deployment))\\b"),
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
        QStringLiteral("Boundary"),        QStringLiteral("Enterprise_Boundary"), QStringLiteral("System_Boundary"),
        QStringLiteral("Container_Boundary"), QStringLiteral("Deployment_Node"), QStringLiteral("Node"),
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

// `ContainerDb_Ext` → a container, database variant, external.
std::optional<Element> elementOf(QString macro)
{
    Element element;
    element.external = macro.endsWith(QLatin1String("_Ext"));
    if (element.external)
        macro.chop(4);
    element.variant = macro.endsWith(QLatin1String("Db"))      ? QStringLiteral("database")
                      : macro.endsWith(QLatin1String("Queue")) ? QStringLiteral("queue")
                                                               : QStringLiteral("box");
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
        // Nodes by key: a key declared twice is one element, the last
        // declaration winning, at the place of the first.
        QHash<QString, qsizetype> indexOf;
        QStringList boundaries;
        QString family = QStringLiteral("C4Context");
        QString heading;
        const auto parentProps = [&](QJsonObject &props) {
            if (!boundaries.isEmpty())
                props.insert(QStringLiteral("parentBoundary"), boundaries.last());
        };
        const auto setNode = [&](const QString &type, const QString &key, const QJsonObject &props) {
            if (const auto it = indexOf.constFind(key); it != indexOf.constEnd()) {
                model.nodes[*it] = {type, key, props};
                return;
            }
            indexOf.insert(key, model.nodes.size());
            model.nodes.append({type, key, props});
        };

        static const QRegularExpression titleLine(QStringLiteral("^title\\s+(.+)$"),
                                                  QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression accessibility(QStringLiteral("^acc(?:Title|Descr)\\b"));
        for (const QString &line : mermaidLines(source)) {
            if (const QRegularExpressionMatch h = header().match(line); h.hasMatch()) {
                family = h.captured(1);
                continue;
            }
            if (const QRegularExpressionMatch t = titleLine.match(line); t.hasMatch()) {
                heading = t.captured(1).trimmed();
                continue;
            }
            if (line == QLatin1String("}")) {
                if (!boundaries.isEmpty())
                    boundaries.removeLast();
                continue;
            }
            if (accessibility.match(line).hasMatch())
                continue;
            const QRegularExpressionMatch c = call().match(line);
            if (!c.hasMatch()) {
                model.warnings.append(QStringLiteral("Not understood: %1").arg(line));
                continue;
            }
            const QString macro = c.captured(1);
            const QStringList args = argsOf(c.captured(2));
            const bool opens = !c.captured(3).isEmpty();

            if (isBoundary(macro) && !args.value(0).isEmpty()) {
                const QString key = args[0];
                QJsonObject props{
                    {QStringLiteral("name"), args.value(1).isEmpty() ? key : args[1]},
                    {QStringLiteral("boundaryKind"),
                     macro.startsWith(QLatin1String("Node")) ? QStringLiteral("Deployment_Node") : macro}};
                parentProps(props);
                setNode(QStringLiteral("c4boundary"), key, props);
                if (opens)
                    boundaries.append(key);
                continue;
            }

            if (relations().match(macro).hasMatch()) {
                const int offset = macro.startsWith(QLatin1String("RelIndex")) ? 1 : 0;
                if (args.size() < offset + 2) {
                    model.warnings.append(QStringLiteral("Not understood: %1").arg(line));
                    continue;
                }
                model.links.append({QStringLiteral("c4relation"), args[offset], args[offset + 1],
                                    QJsonObject{{QStringLiteral("label"), args.value(offset + 2)},
                                                {QStringLiteral("technology"), args.value(offset + 3)},
                                                {QStringLiteral("description"), args.value(offset + 4)},
                                                {QStringLiteral("bidirectional"), macro.startsWith(QLatin1String("BiRel"))}}});
                continue;
            }

            const std::optional<Element> element = elementOf(macro);
            if (!element || args.value(0).isEmpty()) {
                if (!presentation().match(macro).hasMatch())
                    model.warnings.append(QStringLiteral("Not understood: %1").arg(line));
                continue;
            }
            // Containers and components carry a technology before their
            // description; people and systems do not.
            const bool structural = isStructural(element->kind);
            QJsonObject props{{QStringLiteral("name"), args.value(1).isEmpty() ? args[0] : args[1]},
                              {QStringLiteral("kind"), element->kind},
                              {QStringLiteral("variant"), element->variant},
                              {QStringLiteral("external"), element->external},
                              {QStringLiteral("description"), structural ? args.value(3) : args.value(2)},
                              {QStringLiteral("technology"), structural ? args.value(2) : QString()}};
            parentProps(props);
            setNode(QStringLiteral("c4"), args[0], props);
        }
        for (const Model::Edge &edge : model.links) {
            for (const QString &end : {edge.from, edge.to}) {
                if (!indexOf.contains(end))
                    model.warnings.append(QStringLiteral("Relation to an undeclared element: %1").arg(end));
            }
        }
        model.meta = QJsonObject{{QStringLiteral("rankdir"), QStringLiteral("LR")},
                                 {QStringLiteral("family"), family},
                                 {QStringLiteral("title"), heading}};
        return true;
    }

    QString serialize(const Board &board, const QJsonObject &meta) const override
    {
        QSet<QString> used;
        QHash<QString, QString> keys;
        std::vector<const Shape *> shapes;
        for (const auto &s : board.shapes()) {
            shapes.push_back(s.get());
            keys.insert(s->id, keyFor(s->key, s->displayText(), used));
        }
        const QString family = meta.value(QStringLiteral("family")).toString();
        QStringList out{family.isEmpty() ? QStringLiteral("C4Context") : family};
        const QString heading = meta.value(QStringLiteral("title")).toString();
        if (!heading.isEmpty())
            out.append(QStringLiteral("  title ") + heading);

        // The key of a shape's boundary; empty at the top level, and for a
        // parent that is gone (the shape then stays written, at the top).
        const auto parentKey = [&](const Shape *shape) -> QString {
            if (shape->parentBoundary.isEmpty())
                return {};
            const Shape *parent = board.shapeByReference(shape->parentBoundary);
            return parent && parent != shape && parent->type == QLatin1String("c4boundary") ? keys.value(parent->id)
                                                                                            : QString();
        };
        QSet<const Shape *> emitted;
        std::function<void(const Shape *, int)> write = [&](const Shape *shape, int depth) {
            if (emitted.contains(shape))
                return;
            emitted.insert(shape);
            const QString indent(depth * 2, QLatin1Char(' '));
            const QString key = keys.value(shape->id);
            if (const auto *boundary = dynamic_cast<const C4BoundaryShape *>(shape)) {
                const QString macro = boundary->boundaryKind.isEmpty() ? QStringLiteral("System_Boundary")
                                                                       : boundary->boundaryKind;
                out.append(QStringLiteral("%1%2(%3, %4) {").arg(indent, macro, key, quote(boundary->name)));
                for (const Shape *child : shapes) {
                    if (parentKey(child) == key)
                        write(child, depth + 1);
                }
                out.append(indent + QLatin1Char('}'));
                return;
            }
            const auto *element = dynamic_cast<const C4ElementShape *>(shape);
            if (!element)
                return;
            static const QHash<QString, QString> bases{{QStringLiteral("person"), QStringLiteral("Person")},
                                                       {QStringLiteral("system"), QStringLiteral("System")},
                                                       {QStringLiteral("container"), QStringLiteral("Container")},
                                                       {QStringLiteral("component"), QStringLiteral("Component")}};
            const QString macro = bases.value(element->kind, QStringLiteral("System"))
                                  + (element->variant == QLatin1String("database") ? QStringLiteral("Db")
                                     : element->variant == QLatin1String("queue")  ? QStringLiteral("Queue")
                                                                                   : QString())
                                  + (element->external ? QStringLiteral("_Ext") : QString());
            QStringList args{key, quote(element->name)};
            if (isStructural(element->kind))
                args.append(quote(element->technology));
            args.append(quote(element->description));
            out.append(indent + macro + QLatin1Char('(') + args.join(QStringLiteral(", ")) + QLatin1Char(')'));
        };
        for (const Shape *shape : shapes) {
            if (parentKey(shape).isEmpty())
                write(shape, 1);
        }
        // Boundaries whose parenthood loops have no root: written at the top
        // rather than lost.
        for (const Shape *shape : shapes)
            write(shape, 1);

        for (const auto &l : board.links()) {
            const QString from = keys.value(l->from), to = keys.value(l->to);
            if (from.isEmpty() || to.isEmpty())
                continue;
            const auto *relation = dynamic_cast<const C4RelationLink *>(l.get());
            QStringList args{from, to, quote(l->label)};
            const QString technology = relation ? relation->technology : QString();
            const QString description = relation ? relation->description : QString();
            // The arguments are positional: a description needs the
            // technology before it, even empty.
            if (!technology.isEmpty() || !description.isEmpty())
                args.append(quote(technology));
            if (!description.isEmpty())
                args.append(quote(description));
            out.append(QStringLiteral("  %1(%2)").arg(relation && relation->bidirectional ? QStringLiteral("BiRel")
                                                                                         : QStringLiteral("Rel"),
                                                     args.join(QStringLiteral(", "))));
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
