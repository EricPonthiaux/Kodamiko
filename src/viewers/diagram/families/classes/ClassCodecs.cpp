// SPDX-License-Identifier: GPL-3.0-only

// Mermaid and PlantUML class diagrams (sidecar's fence/mermaid-class.js and
// fence/plantuml-class.js).
//
// Relations are normalized so that the UML marker sits at the link's `to`
// end: `A <|-- B` (the triangle at A) becomes from=B, to=A, kind=extension.

#include "Classes.h"
#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/SourceText.h"
#include "viewers/diagram/families/Families.h"

#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>

namespace diagram::classes {

namespace {

using namespace diagram::source;

// What a source says of one class, before it is a shape.
struct ClassNode {
    QString key;
    QString name;
    QString stereotype;
    QStringList attributes;
    QStringList methods;
};

struct ClassNodes {
    QHash<QString, int> index;
    QList<ClassNode> list;

    // The class of `key`, created with `name` the first time.
    ClassNode &get(const QString &key, const QString &name)
    {
        int at = index.value(key, -1);
        if (at < 0) {
            at = int(list.size());
            index.insert(key, at);
            list.append(ClassNode{key, name, {}, {}, {}});
        }
        return list[at];
    }
};

// A relation as written: `left [leftCard] token [rightCard] right [: label]`.
struct WrittenRelation {
    QString leftKey;
    QString rightKey;
    QString token;
    QString leftCard;
    QString rightCard;
    QString label;
};

const UmlRelation *findUmlRelation(const QString &token)
{
    const QList<UmlRelation> &relations = umlRelations();
    const auto found =
        std::ranges::find_if(relations, [&](const UmlRelation &candidate) { return candidate.token == token; });
    return found == relations.end() ? nullptr : &*found;
}

void addRelation(Model &model, const WrittenRelation &written)
{
    const UmlRelation *relation = findUmlRelation(written.token);
    if (!relation)
        return;
    // A marker written on the left makes the left class the target.
    const bool left = relation->targetOnLeft;
    model.links.append({QStringLiteral("relation"), left ? written.rightKey : written.leftKey,
                        left ? written.leftKey : written.rightKey,
                        QJsonObject{{QStringLiteral("kind"), relation->kind},
                                    {QStringLiteral("label"), written.label},
                                    {QStringLiteral("fromCard"), left ? written.rightCard : written.leftCard},
                                    {QStringLiteral("toCard"), left ? written.leftCard : written.rightCard}}});
}

void finishModel(Model &model, const ClassNodes &nodes, const QJsonObject &meta)
{
    for (const ClassNode &node : nodes.list) {
        model.nodes.append(
            {QStringLiteral("class"), node.key,
             QJsonObject{{QStringLiteral("name"), node.name},
                         {QStringLiteral("stereotype"),
                          node.stereotype.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(node.stereotype)},
                         {QStringLiteral("attributes"), toJsonArray(node.attributes)},
                         {QStringLiteral("methods"), toJsonArray(node.methods)}}});
    }
    model.meta = meta;
}

// The ends of every relation, by source key, in the serializers' order.
struct ClassOut {
    QString key;
    const ClassShape *shape;
};

QList<ClassOut> classesOf(const Board &board, QHash<QString, QString> &keys)
{
    QSet<QString> used;
    QList<ClassOut> result;
    for (const auto &s : board.shapes()) {
        const auto *c = dynamic_cast<const ClassShape *>(s.get());
        if (!c)
            continue;
        const QString key = keyFor(c->key, c->name, used);
        keys.insert(c->id, key);
        result.append({key, c});
    }
    return result;
}

QStringList relationLines(const Board &board, const QHash<QString, QString> &keys, const QString &indent)
{
    QStringList out;
    for (const auto &l : board.links()) {
        const QString from = keys.value(l->from);
        const QString to = keys.value(l->to);
        if (from.isEmpty() || to.isEmpty())
            continue;
        const auto *relation = dynamic_cast<const ClassLink *>(l.get());
        const QString kind = relation ? relation->kind : QStringLiteral("association");
        const QString fromCard =
            relation && !relation->fromCard.isEmpty() ? QStringLiteral(" \"%1\"").arg(relation->fromCard) : QString();
        const QString toCard =
            relation && !relation->toCard.isEmpty() ? QStringLiteral("\"%1\" ").arg(relation->toCard) : QString();
        const QString label = l->label.isEmpty() ? QString() : QStringLiteral(" : ") + l->label;
        QString line = indent + from + fromCard + QLatin1Char(' ') + umlRelationOutput(kind) + QLatin1Char(' ') +
                       toCard + to + label;
        line.replace(QLatin1Char('\n'), QLatin1Char(' '));
        out.append(line);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Mermaid

// A class name. Mermaid is not ASCII-only here, and an agent writing in the
// user's language names its classes `Société` without a second thought.
const QString mermaidName = QStringLiteral("[\\p{L}\\p{N}_.`~]+");

const QRegularExpression &mermaidHeader()
{
    static const QRegularExpression pattern(QStringLiteral("^classDiagram(-v2)?\\b"));
    return pattern;
}

// `~T~` generics display as <T>; `` `name` `` backticks are dropped.
QString mermaidDisplayName(QString raw)
{
    static const QRegularExpression generics(QStringLiteral("~([^~]*)~"));
    raw.replace(generics, QStringLiteral("<\\1>"));
    raw.remove(QLatin1Char('`'));
    return raw;
}

// Reads a Mermaid class diagram one statement at a time.
class MermaidReader {
public:
    explicit MermaidReader(Model &model)
        : model_(model)
    {
    }

    void read(const QString &stmt)
    {
        if (mermaidHeader().match(stmt).hasMatch())
            return;
        const bool understood = readDirection(stmt) || readBodyLine(stmt) || readDeclaration(stmt) ||
                                readAnnotation(stmt) || readRelation(stmt) || isSkipped(stmt) ||
                                readExternalMember(stmt);
        if (!understood)
            model_.warnings.append(QStringLiteral("Not understood: %1").arg(stmt));
    }

    void finish() { finishModel(model_, nodes_, QJsonObject{{QStringLiteral("rankdir"), rankdir_}}); }

private:
    ClassNode &classNamed(const QString &rawName)
    {
        static const QRegularExpression genericsOut(QStringLiteral("~[^~]*~"));
        QString key = rawName;
        key.remove(QLatin1Char('`'));
        key.remove(genericsOut);
        return nodes_.get(key.trimmed(), mermaidDisplayName(rawName));
    }

    static void addMember(ClassNode &node, const QString &raw)
    {
        static const QRegularExpression stereotypeLine(QStringLiteral("^<<(.+)>>$"));
        const QString line = mermaidDisplayName(raw.trimmed());
        if (line.isEmpty())
            return;
        const QRegularExpressionMatch st = stereotypeLine.match(line);
        if (st.hasMatch()) {
            node.stereotype = st.captured(1);
            return;
        }
        (line.contains(QLatin1Char('(')) ? node.methods : node.attributes).append(line);
    }

    bool readDirection(const QString &stmt)
    {
        static const QRegularExpression direction(QStringLiteral("^direction\\s+(TB|BT|RL|LR)"));
        const QRegularExpressionMatch m = direction.match(stmt);
        if (!m.hasMatch())
            return false;
        rankdir_ = m.captured(1);
        return true;
    }

    bool readBodyLine(const QString &stmt)
    {
        static const QRegularExpression closing(QStringLiteral("\\}$"));
        if (body_ < 0)
            return false;
        if (stmt == QLatin1String("}")) {
            body_ = -1;
            return true;
        }
        QString line = stmt;
        line.remove(closing);
        addMember(nodes_.list[body_], line);
        if (stmt.endsWith(QLatin1Char('}')))
            body_ = -1;
        return true;
    }

    // class Name / class Name["Label"] / class Name {
    bool readDeclaration(const QString &stmt)
    {
        static const QRegularExpression declaration(QStringLiteral("^class\\s+(") + mermaidName +
                                                        QStringLiteral(")\\s*(?:\\[\"([^\"]*)\"\\])?\\s*(\\{)?\\s*$"),
                                                    QRegularExpression::UseUnicodePropertiesOption);
        const QRegularExpressionMatch m = declaration.match(stmt);
        if (!m.hasMatch())
            return false;
        ClassNode &node = classNamed(m.captured(1));
        if (!m.captured(2).isEmpty())
            node.name = m.captured(2);
        if (!m.captured(3).isEmpty())
            body_ = nodes_.index.value(node.key);
        return true;
    }

    // <<interface>> Name, a standalone annotation.
    bool readAnnotation(const QString &stmt)
    {
        static const QRegularExpression annotation(QStringLiteral("^<<(.+)>>\\s+(") + mermaidName +
                                                       QStringLiteral(")$"),
                                                   QRegularExpression::UseUnicodePropertiesOption);
        const QRegularExpressionMatch m = annotation.match(stmt);
        if (!m.hasMatch())
            return false;
        classNamed(m.captured(2)).stereotype = m.captured(1);
        return true;
    }

    // [left, "card"], operator, ["card"] right, [: label]
    bool readRelation(const QString &stmt)
    {
        static const QRegularExpression relationPattern(
            QStringLiteral("^(") + mermaidName + QStringLiteral(")\\s*(?:\"([^\"]*)\"\\s*)?(") + umlRelationPattern() +
                QStringLiteral(")\\s*(?:\"([^\"]*)\"\\s*)?(") + mermaidName + QStringLiteral(")\\s*(?::\\s*(.*))?$"),
            QRegularExpression::UseUnicodePropertiesOption);
        const QRegularExpressionMatch m = relationPattern.match(stmt);
        if (!m.hasMatch())
            return false;
        const QString leftKey = classNamed(m.captured(1)).key;
        const QString rightKey = classNamed(m.captured(5)).key;
        addRelation(model_, {leftKey, rightKey, m.captured(3), m.captured(2), m.captured(4), m.captured(6).trimmed()});
        return true;
    }

    static bool isSkipped(const QString &stmt)
    {
        static const QRegularExpression skip(
            QStringLiteral("^(?:direction\\b|note\\b|namespace\\b|style\\b|classDef\\b|cssClass\\b|callback\\b|link\\b|"
                           "click\\b|href\\b|accTitle|accDescr|\\})"));
        return skip.match(stmt).hasMatch();
    }

    // Name : +member()
    bool readExternalMember(const QString &stmt)
    {
        static const QRegularExpression memberLine(QStringLiteral("^(") + mermaidName +
                                                       QStringLiteral(")\\s*:\\s*(.+)$"),
                                                   QRegularExpression::UseUnicodePropertiesOption);
        const QRegularExpressionMatch m = memberLine.match(stmt);
        if (!m.hasMatch())
            return false;
        addMember(classNamed(m.captured(1)), m.captured(2));
        return true;
    }

    Model &model_;
    ClassNodes nodes_;
    QString rankdir_ = QStringLiteral("TB");
    int body_ = -1; // the class whose { } body we are inside
};

class MermaidClass : public Codec {
public:
    QString id() const override { return QStringLiteral("mermaid-class"); }

    QString language() const override { return QStringLiteral("mermaid"); }

    QString title() const override { return QStringLiteral("Class diagram"); }

    const Family &family() const override { return families::classes(); }

    bool matches(const QString &source) const override
    {
        // Comments (a title) may come first.
        for (const QString &line : mermaidLines(source))
            return mermaidHeader().match(line).hasMatch();
        return false;
    }

    bool parse(const QString &source, Model &model, QString *) const override
    {
        MermaidReader reader(model);
        for (const QString &stmt : mermaidLines(source))
            reader.read(stmt);
        reader.finish();
        return true;
    }

    QString serialize(const Board &board, const QJsonObject &meta) const override
    {
        QStringList out{QStringLiteral("classDiagram")};
        const QString rankdir = meta.value(QStringLiteral("rankdir")).toString();
        if (!rankdir.isEmpty() && rankdir != QLatin1String("TB"))
            out.append(QStringLiteral("  direction ") + rankdir);
        QHash<QString, QString> keys;
        for (const ClassOut &c : classesOf(board, keys)) {
            QStringList members;
            if (!c.shape->stereotype.isEmpty())
                members.append(QStringLiteral("<<%1>>").arg(c.shape->stereotype));
            // Generics go back to Mermaid's ~T~ form (stereotype guillemets
            // stay).
            for (QString m : c.shape->attributes + c.shape->methods) {
                m.replace(QLatin1Char('<'), QLatin1Char('~'));
                m.replace(QLatin1Char('>'), QLatin1Char('~'));
                members.append(m);
            }
            if (members.isEmpty() && c.shape->name == c.key) {
                out.append(QStringLiteral("  class ") + c.key);
                continue;
            }
            QString name = c.shape->name;
            name.replace(QLatin1Char('"'), QLatin1Char('\''));
            name.replace(QLatin1Char('\n'), QLatin1Char(' '));
            const QString label = c.shape->name != c.key ? QStringLiteral("[\"%1\"]").arg(name) : QString();
            out.append(QStringLiteral("  class %1%2 {").arg(c.key, label));
            for (const QString &m : members)
                out.append(QStringLiteral("    ") + m);
            out.append(QStringLiteral("  }"));
        }
        out += relationLines(board, keys, QStringLiteral("  "));
        return out.join(QLatin1Char('\n'));
    }

    QString blank() const override
    {
        return QStringLiteral("classDiagram\n  class Element {\n    +String name\n  }\n  class Service {\n"
                              "    +process()\n  }\n  Element --> Service : uses");
    }
};

// ---------------------------------------------------------------------------
// PlantUML

const QString plantumlKinds =
    QStringLiteral("abstract\\s+class|abstract|class|interface|enum|annotation|entity|struct|exception|protocol");

// `-left->`, `-l->`, `-[#red,dashed]->`, `-->>`… reduce to the canonical
// two-character body the relation pattern knows: infixes stripped, dashes
// collapsed.
QString canonicalArrows(QString stmt)
{
    static const QRegularExpression direction(QStringLiteral("-(?:left|right|up|down|le?|ri?|u|d)-"),
                                              QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression brackets(QStringLiteral("\\[[^\\]]*\\]"));
    static const QRegularExpression dashes(QStringLiteral("-{3,}"));
    static const QRegularExpression dots(QStringLiteral("\\.{3,}"));
    stmt.replace(direction, QStringLiteral("--"));
    stmt.remove(brackets);
    stmt.replace(dashes, QStringLiteral("--"));
    stmt.replace(dots, QStringLiteral(".."));
    return stmt;
}

// The stereotype a PlantUML declaration keyword implies: none for a plain
// class, "abstract" for both abstract forms, the keyword itself otherwise.
QString keywordStereotype(const QString &kind)
{
    if (kind.startsWith(QLatin1String("abstract")))
        return QStringLiteral("abstract");
    if (kind == QLatin1String("class"))
        return {};
    return kind;
}

// Reads a PlantUML class diagram one statement at a time.
class PlantumlReader {
public:
    explicit PlantumlReader(Model &model)
        : model_(model)
    {
    }

    void read(const QString &stmt)
    {
        const bool understood = readBodyLine(stmt) || readDeclaration(stmt) || isSkipped(stmt) || readRelation(stmt) ||
                                readExternalMember(stmt);
        if (!understood)
            model_.warnings.append(QStringLiteral("Not understood: %1").arg(stmt));
    }

    void finish() { finishModel(model_, nodes_, QJsonObject{}); }

private:
    ClassNode &classNamed(const QString &rawName, const QString &alias = QString())
    {
        static const QRegularExpression generics(QStringLiteral("<[^>]*>"));
        QString key = rawName;
        key.remove(generics);
        ClassNode &node = nodes_.get(key.trimmed(), alias.isEmpty() ? rawName : alias);
        if (!alias.isEmpty())
            node.name = alias;
        return node;
    }

    static void addMember(ClassNode &node, const QString &raw)
    {
        static const QRegularExpression modifiers(QStringLiteral("\\{(?:static|abstract|field|method)\\}\\s*"),
                                                  QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression decoration(QStringLiteral("^[-.=_]{2,}"));
        QString line = raw;
        line.remove(modifiers);
        line = line.trimmed();
        if (line.isEmpty() || decoration.match(line).hasMatch())
            return; // a separator's decoration
        (line.contains(QLatin1Char('(')) ? node.methods : node.attributes).append(line);
    }

    bool readBodyLine(const QString &stmt)
    {
        static const QRegularExpression closing(QStringLiteral("\\}$"));
        if (body_ < 0)
            return false;
        if (stmt == QLatin1String("}")) {
            body_ = -1;
            return true;
        }
        QString line = stmt;
        line.remove(closing);
        addMember(nodes_.list[body_], line);
        if (stmt.endsWith(QLatin1Char('}')))
            body_ = -1;
        return true;
    }

    // `"Display" as Key`, `Key`, or `Key as "Display"`.
    ClassNode &declaredClass(const QRegularExpressionMatch &m)
    {
        const bool isQuoted = !m.captured(1).isEmpty();
        if (!isQuoted)
            return classNamed(m.captured(3), m.captured(4).isEmpty() ? QString() : plantumlText(m.captured(4)));
        if (m.captured(2).isEmpty())
            return classNamed(plantumlText(m.captured(1)));
        return classNamed(m.captured(2), plantumlText(m.captured(1)));
    }

    bool readDeclaration(const QString &stmt)
    {
        // Names are not ASCII-only (unlike sidecar's \w): `class Société`.
        constexpr auto options =
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption;
        static const QRegularExpression declaration(
            QStringLiteral("^(?:") + plantumlKinds +
                QStringLiteral(
                    ")\\s+(?:\"([^\"]+)\"(?:\\s+as\\s+([\\w.]+))?|([\\w.]+(?:<[^>]*>)?)(?:\\s+as\\s+\"([^\"]+)\")?)"
                    "\\s*(?:<<[^>]*>>\\s*)?(\\{)?\\s*$"),
            options);
        static const QRegularExpression declarationKind(QStringLiteral("^(") + plantumlKinds + QStringLiteral(")"),
                                                        QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression stereotypePattern(QStringLiteral("<<\\s*([^>]*?)\\s*>>"));
        const QRegularExpressionMatch m = declaration.match(stmt);
        if (!m.hasMatch())
            return false;
        ClassNode &node = declaredClass(m);
        const QString kindStereotype = keywordStereotype(declarationKind.match(stmt).captured(1).toLower());
        if (!kindStereotype.isEmpty())
            node.stereotype = kindStereotype;
        const QRegularExpressionMatch st = stereotypePattern.match(stmt);
        if (st.hasMatch() && !st.captured(1).isEmpty())
            node.stereotype = st.captured(1);
        if (!m.captured(5).isEmpty())
            body_ = nodes_.index.value(node.key);
        return true;
    }

    static bool isSkipped(const QString &stmt)
    {
        static const QRegularExpression skip(
            QStringLiteral("^(?:package\\b|namespace\\b|together\\b|note\\b|end\\s+note|"
                           "\\.\\.|__|==|--$|\\})"),
            QRegularExpression::CaseInsensitiveOption);
        return skip.match(stmt).hasMatch();
    }

    // After infix stripping, the arrows are Mermaid's.
    bool readRelation(const QString &stmt)
    {
        static const QRegularExpression relationPattern(
            QStringLiteral("^([\\w.\"<>]+)\\s*(?:\"([^\"]*)\"\\s*)?(") + umlRelationPattern() +
                QStringLiteral(")\\s*(?:\"([^\"]*)\"\\s*)?([\\w.\"<>]+)\\s*(?::\\s*(.*))?$"),
            QRegularExpression::UseUnicodePropertiesOption);
        static const QRegularExpression labelArrow(QStringLiteral("\\s*[<>]$"));
        const QRegularExpressionMatch m = relationPattern.match(canonicalArrows(stmt));
        if (!m.hasMatch())
            return false;
        QString left = m.captured(1);
        QString right = m.captured(5);
        left.remove(QLatin1Char('"'));
        right.remove(QLatin1Char('"'));
        const QString leftKey = classNamed(left).key;
        const QString rightKey = classNamed(right).key;
        QString label = m.captured(6);
        label.remove(labelArrow);
        addRelation(model_, {leftKey, rightKey, m.captured(3), m.captured(2), m.captured(4), label.trimmed()});
        return true;
    }

    // An external member: X : +method()
    bool readExternalMember(const QString &stmt)
    {
        static const QRegularExpression memberLine(QStringLiteral("^([\\w.]+)\\s*:\\s*(.+)$"),
                                                   QRegularExpression::UseUnicodePropertiesOption);
        const QRegularExpressionMatch m = memberLine.match(stmt);
        if (!m.hasMatch())
            return false;
        addMember(classNamed(m.captured(1)), m.captured(2));
        return true;
    }

    Model &model_;
    ClassNodes nodes_;
    int body_ = -1; // the class whose { } body we are inside
};

class PlantumlClass : public Codec {
public:
    QString id() const override { return QStringLiteral("plantuml-class"); }

    QString language() const override { return QStringLiteral("plantuml"); }

    QString title() const override { return QStringLiteral("Class diagram"); }

    const Family &family() const override { return families::classes(); }

    bool matches(const QString &source) const override
    {
        static const QRegularExpression keyword(
            QStringLiteral("^(?:abstract\\s+class|abstract|class|interface|enum|annotation)\\b"),
            QRegularExpression::CaseInsensitiveOption);
        const QStringList lines = plantumlLines(source);
        return std::ranges::any_of(lines, [](const QString &line) { return keyword.match(line).hasMatch(); });
    }

    bool parse(const QString &source, Model &model, QString *) const override
    {
        PlantumlReader reader(model);
        for (const QString &stmt : plantumlLines(source))
            reader.read(stmt);
        reader.finish();
        return true;
    }

    QString serialize(const Board &board, const QJsonObject &) const override
    {
        static const QHash<QString, QString> keywords{
            {QStringLiteral("interface"), QStringLiteral("interface")},
            {QStringLiteral("enumeration"), QStringLiteral("enum")},
            {QStringLiteral("enum"), QStringLiteral("enum")},
            {QStringLiteral("abstract"), QStringLiteral("abstract class")},
            {QStringLiteral("annotation"), QStringLiteral("annotation")},
        };
        QStringList out{QStringLiteral("@startuml")};
        QHash<QString, QString> keys;
        for (const ClassOut &c : classesOf(board, keys)) {
            const QString keyword = keywords.value(c.shape->stereotype, QStringLiteral("class"));
            QString name = c.shape->name;
            name.replace(QLatin1Char('"'), QLatin1Char('\''));
            name.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
            const QString label =
                !c.shape->name.isEmpty() && c.shape->name != c.key ? QStringLiteral("\"%1\" as ").arg(name) : QString();
            const QString st = keyword == QLatin1String("class") && !c.shape->stereotype.isEmpty()
                                   ? QStringLiteral(" <<%1>>").arg(c.shape->stereotype)
                                   : QString();
            const QStringList members = c.shape->attributes + c.shape->methods;
            const QString head = keyword + QLatin1Char(' ') + label + c.key + st;
            if (members.isEmpty()) {
                out.append(head);
                continue;
            }
            out.append(head + QStringLiteral(" {"));
            for (const QString &m : members)
                out.append(QStringLiteral("  ") + m);
            out.append(QStringLiteral("}"));
        }
        out += relationLines(board, keys, QString());
        out.append(QStringLiteral("@enduml"));
        return out.join(QLatin1Char('\n'));
    }

    QString blank() const override
    {
        return QStringLiteral("@startuml\nclass Element {\n  +String name\n}\nclass Service {\n  +process()\n}\n"
                              "Element --> Service : uses\n@enduml");
    }
};

} // namespace

} // namespace diagram::classes

namespace diagram::families {

QList<const Codec *> classCodecs()
{
    static const classes::MermaidClass mermaid;
    static const classes::PlantumlClass plantuml;
    return {&mermaid, &plantuml};
}

} // namespace diagram::families
