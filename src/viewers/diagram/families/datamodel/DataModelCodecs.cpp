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

// Reads an erDiagram line by line: a block of columns stays open across
// lines until its `}`.
class ErReader {
public:
    explicit ErReader(Model &model)
        : model_(model)
    {
    }

    void read(const QString &line)
    {
        if (header().match(line).hasMatch())
            return;
        if (!body_.isEmpty()) {
            readBodyLine(line);
            return;
        }
        if (readBlockOpening(line) || readRelation(line) || readEntityAlone(line))
            return;
        model_.warnings.append(QStringLiteral("Not understood: ") + line);
    }

    // Closes a block the source left open.
    void finish()
    {
        if (!body_.isEmpty())
            closeBody();
    }

private:
    Model::Node &entity(const QString &key)
    {
        if (!index_.contains(key)) {
            index_.insert(key, model_.nodes.size());
            model_.nodes.append(
                {QStringLiteral("entity"), key,
                 QJsonObject{{QStringLiteral("name"), key}, {QStringLiteral("attributes"), QJsonArray()}}});
        }
        return model_.nodes[index_.value(key)];
    }

    static void rename(Model::Node &node, const QString &alias)
    {
        if (!alias.trimmed().isEmpty())
            node.props.insert(QStringLiteral("name"), alias.trimmed());
    }

    void closeBody()
    {
        entity(body_).props.insert(QStringLiteral("attributes"), attributes_);
        body_.clear();
        attributes_ = QJsonArray();
    }

    // A line of an open block: a column, the block's end, or both.
    void readBodyLine(const QString &line)
    {
        const bool closes = line.endsWith(QLatin1Char('}'));
        const QString field = closes ? line.chopped(1).trimmed() : line;
        if (!field.isEmpty())
            attributes_.append(field);
        if (closes)
            closeBody();
    }

    bool readBlockOpening(const QString &line)
    {
        const QRegularExpressionMatch open = entityBlock().match(line);
        if (!open.hasMatch())
            return false;
        Model::Node &node = entity(open.captured(1));
        rename(node, open.captured(2));
        attributes_ = node.props.value(QStringLiteral("attributes")).toArray();
        body_ = open.captured(1);
        // `EMPTY {}` and `ORDER { uuid id PK }` on one line.
        if (const QString rest = open.captured(3).trimmed(); !rest.isEmpty())
            readBodyLine(rest);
        return true;
    }

    bool readRelation(const QString &line)
    {
        const QRegularExpressionMatch rel = relation().match(line);
        if (!rel.hasMatch())
            return false;
        entity(rel.captured(1));
        entity(rel.captured(5));
        QJsonObject props{{QStringLiteral("fromCard"), rel.captured(2)},
                          {QStringLiteral("toCard"), rel.captured(4)},
                          {QStringLiteral("label"), mermaidUnquote(rel.captured(6))}};
        if (rel.captured(3) == QLatin1String(".."))
            props.insert(QStringLiteral("identifying"), false);
        model_.links.append({QStringLiteral("relationship"), rel.captured(1), rel.captured(5), props});
        return true;
    }

    bool readEntityAlone(const QString &line)
    {
        const QRegularExpressionMatch alone = entityAlone().match(line);
        if (!alone.hasMatch())
            return false;
        rename(entity(alone.captured(1)), alone.captured(2));
        return true;
    }

    Model &model_;
    QHash<QString, qsizetype> index_;
    QString body_; // the key of the entity whose block is open
    QJsonArray attributes_;
};

// A table renamed after it was read keeps its key (and its place); Mermaid
// shows the new name as the entity's alias.
QString aliasText(const QString &name, const QString &key)
{
    if (name == key || name.isEmpty())
        return {};
    QString text = name;
    text.replace(QLatin1Char('"'), QLatin1Char('\''));
    return QStringLiteral("[\"%1\"]").arg(text);
}

QString relationLine(const Link &link, const QString &from, const QString &to)
{
    const auto *relationship = dynamic_cast<const RelationshipLink *>(&link);
    const QString fromCard =
        relationship && !relationship->fromCard.isEmpty() ? relationship->fromCard : QStringLiteral("||");
    const QString toCard =
        relationship && !relationship->toCard.isEmpty() ? relationship->toCard : QStringLiteral("o{");
    const QString line = relationship && !relationship->identifying ? QStringLiteral("..") : QStringLiteral("--");
    return QStringLiteral("  %1 %2%3%4 %5 : %6")
        .arg(from, fromCard, line, toCard, to,
             roleText(link.label.isEmpty() ? QStringLiteral("relates to") : link.label));
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
        ErReader reader(model);
        for (const QString &line : mermaidLines(source))
            reader.read(line);
        reader.finish();
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
            out.append(QStringLiteral("  %1%2 {").arg(key, aliasText(name, key)));
            if (table) {
                for (const QString &field : table->attributes)
                    out.append(QStringLiteral("    ") + field);
            }
            out.append(QStringLiteral("  }"));
        }
        for (const auto &l : board.links()) {
            const QString from = keys.value(l->from);
            const QString to = keys.value(l->to);
            if (!from.isEmpty() && !to.isEmpty())
                out.append(relationLine(*l, from, to));
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
