// Mermaid erDiagram (sidecar's fence/mermaid-er.js): entities, their column
// blocks and the cardinality relations between them.

#include "DataModel.h"

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/SourceText.h"
#include "viewers/diagram/families/Families.h"

#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>

namespace diagram::datamodel {

namespace {

using namespace diagram::source;

// A table name. Mermaid accepts more than ASCII here, and an agent writing
// in the user's language names its tables `ÉTABLISSEMENT` without a second
// thought.
const QString namePattern = QStringLiteral("[\\p{L}\\p{N}_.-]+");
// An optional display name: `CUSTOMER["Customer account"]`.
const QString aliasPattern = QStringLiteral("(?:\\s*\\[\\s*\"([^\"]*)\"\\s*\\])?");

const QRegularExpression &header()
{
    static const QRegularExpression pattern(QStringLiteral("^erDiagram\\b"), QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// `A ||--o{ B : label`; `..` instead of `--` for a non-identifying relation.
const QRegularExpression &relation()
{
    static const QRegularExpression pattern(
        QStringLiteral("^(%1)\\s+([|o}{]+)(--|\\.\\.)([|o}{]+)\\s+(%1)\\s*:\\s*(.*)$").arg(namePattern));
    return pattern;
}

const QRegularExpression &entityBlock()
{
    static const QRegularExpression pattern(QStringLiteral("^(%1)%2\\s*\\{(.*)$").arg(namePattern, aliasPattern));
    return pattern;
}

const QRegularExpression &entityAlone()
{
    static const QRegularExpression pattern(QStringLiteral("^(%1)%2$").arg(namePattern, aliasPattern));
    return pattern;
}

// A relation's label, quoted unless it is a single plain word.
QString roleText(const QString &label)
{
    static const QRegularExpression word(QStringLiteral("^[A-Za-z0-9_-]+$"));
    if (word.match(label).hasMatch())
        return label;
    QString text = label;
    text.replace(QLatin1Char('"'), QLatin1Char('\''));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return QLatin1Char('"') + text + QLatin1Char('"');
}

class MermaidEr : public Codec {
public:
    QString id() const override { return QStringLiteral("mermaid-er"); }
    QString language() const override { return QStringLiteral("mermaid"); }
    QString title() const override { return QStringLiteral("Data model"); }
    const Family &family() const override { return families::dataModel(); }

    bool matches(const QString &source) const override
    {
        // Comments (the layout, a title) may come first.
        for (const QString &line : mermaidLines(source))
            return header().match(line).hasMatch();
        return false;
    }

    bool parse(const QString &source, Model &model, QString *) const override
    {
        QHash<QString, qsizetype> index;
        const auto entity = [&](const QString &key) -> Model::Node & {
            if (!index.contains(key)) {
                index.insert(key, model.nodes.size());
                model.nodes.append(
                    {QStringLiteral("entity"), key,
                     QJsonObject{{QStringLiteral("name"), key}, {QStringLiteral("attributes"), QJsonArray()}}});
            }
            return model.nodes[index.value(key)];
        };
        const auto rename = [](Model::Node &node, const QString &alias) {
            if (!alias.trimmed().isEmpty())
                node.props.insert(QStringLiteral("name"), alias.trimmed());
        };

        QString body; // the key of the entity whose block is open
        QJsonArray attributes;
        const auto closeBody = [&] {
            entity(body).props.insert(QStringLiteral("attributes"), attributes);
            body.clear();
            attributes = QJsonArray();
        };
        // A line of an open block: a column, the block's end, or both.
        const auto bodyLine = [&](const QString &line) {
            const bool closes = line.endsWith(QLatin1Char('}'));
            const QString field = closes ? line.chopped(1).trimmed() : line;
            if (!field.isEmpty())
                attributes.append(field);
            if (closes)
                closeBody();
        };
        for (const QString &line : mermaidLines(source)) {
            if (header().match(line).hasMatch())
                continue;
            if (!body.isEmpty()) {
                bodyLine(line);
                continue;
            }
            if (const QRegularExpressionMatch open = entityBlock().match(line); open.hasMatch()) {
                Model::Node &node = entity(open.captured(1));
                rename(node, open.captured(2));
                attributes = node.props.value(QStringLiteral("attributes")).toArray();
                body = open.captured(1);
                // `EMPTY {}` and `ORDER { uuid id PK }` on one line.
                if (const QString rest = open.captured(3).trimmed(); !rest.isEmpty())
                    bodyLine(rest);
                continue;
            }
            if (const QRegularExpressionMatch rel = relation().match(line); rel.hasMatch()) {
                entity(rel.captured(1));
                entity(rel.captured(5));
                QJsonObject props{{QStringLiteral("fromCard"), rel.captured(2)},
                                  {QStringLiteral("toCard"), rel.captured(4)},
                                  {QStringLiteral("label"), mermaidUnquote(rel.captured(6))}};
                if (rel.captured(3) == QLatin1String(".."))
                    props.insert(QStringLiteral("identifying"), false);
                model.links.append({QStringLiteral("relationship"), rel.captured(1), rel.captured(5), props});
                continue;
            }
            if (const QRegularExpressionMatch alone = entityAlone().match(line); alone.hasMatch()) {
                rename(entity(alone.captured(1)), alone.captured(2));
                continue;
            }
            model.warnings.append(QStringLiteral("Not understood: ") + line);
        }
        if (!body.isEmpty())
            closeBody();
        model.meta = QJsonObject{{QStringLiteral("rankdir"), QStringLiteral("LR")}};
        return true;
    }

    QString serialize(const Board &board, const QJsonObject &) const override
    {
        QSet<QString> used;
        QHash<QString, QString> keys;
        QStringList out{QStringLiteral("erDiagram")};
        for (const auto &s : board.shapes()) {
            const auto *table = dynamic_cast<const EntityShape *>(s.get());
            const QString name = table ? table->name : s->displayText();
            const QString key = keyFor(s->key, name, used);
            keys.insert(s->id, key);
            // A table renamed after it was read keeps its key (and its
            // place); Mermaid shows the new name as the entity's alias.
            QString alias;
            if (name != key && !name.isEmpty()) {
                QString text = name;
                text.replace(QLatin1Char('"'), QLatin1Char('\''));
                alias = QStringLiteral("[\"%1\"]").arg(text);
            }
            out.append(QStringLiteral("  %1%2 {").arg(key, alias));
            if (table) {
                for (const QString &field : table->attributes)
                    out.append(QStringLiteral("    ") + field);
            }
            out.append(QStringLiteral("  }"));
        }
        for (const auto &l : board.links()) {
            const QString from = keys.value(l->from), to = keys.value(l->to);
            if (from.isEmpty() || to.isEmpty())
                continue;
            const auto *relationship = dynamic_cast<const RelationshipLink *>(l.get());
            const QString fromCard =
                relationship && !relationship->fromCard.isEmpty() ? relationship->fromCard : QStringLiteral("||");
            const QString toCard =
                relationship && !relationship->toCard.isEmpty() ? relationship->toCard : QStringLiteral("o{");
            const QString line =
                relationship && !relationship->identifying ? QStringLiteral("..") : QStringLiteral("--");
            out.append(QStringLiteral("  %1 %2%3%4 %5 : %6")
                           .arg(from, fromCard, line, toCard, to,
                                roleText(l->label.isEmpty() ? QStringLiteral("relates to") : l->label)));
        }
        return out.join(QLatin1Char('\n'));
    }

    QString blank() const override
    {
        return QStringLiteral(
            "erDiagram\n  ENTITY {\n    uuid id PK\n    string name\n  }\n  ELEMENT {\n    uuid id PK\n"
            "    uuid entity_id FK\n  }\n  ENTITY ||--o{ ELEMENT : contains");
    }
};

} // namespace

} // namespace diagram::datamodel

namespace diagram::families {

QList<const Codec *> dataModelCodecs()
{
    static const datamodel::MermaidEr mermaid;
    return {&mermaid};
}

} // namespace diagram::families
