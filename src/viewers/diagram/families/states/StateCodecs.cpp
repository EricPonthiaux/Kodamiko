// Mermaid and PlantUML state diagrams (sidecar's fence/state-core.js,
// fence/mermaid-state.js and fence/plantuml-state.js). The two notations
// descend from the same one: `[*] --> A`, `A --> B : event`,
// `state "desc" as id` and nested composites `state X { … }`.

#include "States.h"

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/SourceText.h"
#include "viewers/diagram/families/Families.h"

#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>

#include <optional>

namespace diagram::states {

namespace {

using namespace diagram::source;

const QString initialKey = QStringLiteral("[*i]");
const QString finalKey = QStringLiteral("[*f]");

QRegularExpression caseless(const QString &pattern)
{
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
}

// The model both parsers build: nodes keyed by their scoped id, so that two
// composites may each hold an `Idle`, and each its own `[*]`.
class StateModel {
public:
    struct Node {
        QString key;
        QString sourceKey;
        QString type;
        QString text;
        QStringList desc;
        QString parentBoundary;
    };

    static QString scopedKey(const QString &sourceKey, const QString &parent)
    {
        return parent.isEmpty() ? sourceKey : parent + QLatin1Char('.') + sourceKey;
    }

    Node &state(const QString &sourceKey, const QString &parent)
    {
        const QString key = scopedKey(sourceKey, parent);
        if (!index_.contains(key)) {
            index_.insert(key, nodes_.size());
            nodes_.push_back({key, sourceKey, QStringLiteral("state"), sourceKey, {}, parent});
        }
        return nodes_[index_.value(key)];
    }

    Node &container(const QString &sourceKey, const QString &parent, const std::optional<QString> &text)
    {
        Node &node = state(sourceKey, parent);
        node.type = QStringLiteral("stateContainer");
        if (text) {
            node.text = *text;
        } else if (node.text == sourceKey && node.desc.size() == 1) {
            // Mermaid documents `Composite: Visible label` right before
            // `state Composite { … }` as the way to name a composite.
            node.text = node.desc.takeFirst();
        }
        return node;
    }

    // Every composite owns its start and end pseudo-states: their model
    // keys are scoped, the serialization writes the usual `[*]`.
    Node &endpoint(const QString &name, bool from, const QString &parent)
    {
        if (name != QLatin1String("[*]"))
            return state(name, parent);
        const QString key = scopedKey(from ? initialKey : finalKey, parent);
        if (!index_.contains(key)) {
            index_.insert(key, nodes_.size());
            nodes_.push_back({key, {}, from ? QStringLiteral("initial") : QStringLiteral("final"), {}, {}, parent});
        }
        return nodes_[index_.value(key)];
    }

    void transition(const QString &fromName, const QString &toName, const QString &label, const QString &parent)
    {
        const QString from = endpoint(fromName, true, parent).key;
        const QString to = endpoint(toName, false, parent).key;
        links_.append(
            {QStringLiteral("transition"), from, to, QJsonObject{{QStringLiteral("label"), label.trimmed()}}});
    }

    void result(Model &model, const QString &rankdir) const
    {
        for (const Node &node : nodes_) {
            QJsonObject props;
            if (!node.sourceKey.isEmpty())
                props.insert(QStringLiteral("sourceKey"), node.sourceKey);
            if (!node.parentBoundary.isEmpty())
                props.insert(QStringLiteral("parentBoundary"), node.parentBoundary);
            if (node.type != QLatin1String("initial") && node.type != QLatin1String("final") &&
                node.type != QLatin1String("fork") && node.type != QLatin1String("join"))
                props.insert(QStringLiteral("text"), node.text);
            if (node.type == QLatin1String("state"))
                props.insert(QStringLiteral("desc"), toJsonArray(node.desc));
            model.nodes.append({node.type, node.key, props});
        }
        model.links = links_;
        model.meta = QJsonObject{{QStringLiteral("rankdir"), rankdir}};
    }

private:
    QHash<QString, qsizetype> index_;
    std::vector<Node> nodes_;
    QList<Model::Edge> links_;
};

// Serialization shared by both notations: declarations and transitions are
// written inside the composite that owns them; initial and final states
// become `[*]`.
QStringList serializeStates(const Board &board, QStringList out)
{
    const auto &shapes = board.shapes();
    const auto parentOf = [&board](const Shape *s) -> const Shape * {
        return s->parentBoundary.isEmpty() ? nullptr : board.shapeByReference(s->parentBoundary);
    };
    const auto isEndpoint = [](const Shape *s) {
        return s->type == QLatin1String("initial") || s->type == QLatin1String("final");
    };
    const auto textOf = [](const Shape *s) {
        const auto *element = dynamic_cast<const StateElement *>(s);
        return element ? element->sourceText() : s->displayText();
    };

    QSet<QString> used;
    QHash<QString, QString> keys;
    for (const auto &s : shapes) {
        if (isEndpoint(s.get())) {
            keys.insert(s->id, QStringLiteral("[*]"));
            continue;
        }
        // The id the source gave it in its scope, while the board key is
        // still the one that id produces; a copy (a key of its own) or an
        // element created on the board writes its key, scope removed.
        const auto *element = dynamic_cast<const StateElement *>(s.get());
        const QString sourceKey = element ? element->sourceKey : QString();
        QString preferred;
        if (!sourceKey.isEmpty() &&
            (s->key.isEmpty() || s->key == StateModel::scopedKey(sourceKey, s->parentBoundary))) {
            preferred = sourceKey;
        } else if (!s->key.isEmpty()) {
            preferred = s->key;
            const QString scope = s->parentBoundary + QLatin1Char('.');
            if (parentOf(s.get()) && preferred.startsWith(scope) && preferred.size() > scope.size())
                preferred = preferred.mid(scope.size());
        }
        keys.insert(s->id, keyFor(preferred, textOf(s.get()), used));
    }

    // Mermaid forbids a transition between two composites' interiors: one
    // made on the board anyway goes to the top level rather than being lost.
    const auto scopeOf = [&](const Link &l) -> std::optional<const Shape *> {
        const Shape *a = board.shape(l.from);
        const Shape *b = board.shape(l.to);
        if (!a || !b)
            return std::nullopt;
        const Shape *pa = parentOf(a), *pb = parentOf(b);
        return pa == pb ? pa : nullptr;
    };
    const auto escaped = [](QString value) {
        value.replace(QLatin1Char('"'), QLatin1Char('\''));
        value.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
        return value;
    };
    const auto linked = [&board](const Shape *s) {
        for (const auto &l : board.links()) {
            if (l->from == s->id || l->to == s->id)
                return true;
        }
        return false;
    };

    std::function<void(const Shape *, int)> emitScope;
    const auto emitShape = [&](const Shape *s, int depth) {
        if (isEndpoint(s))
            return;
        const QString indent(depth * 2, QLatin1Char(' '));
        const QString key = keys.value(s->id);
        const QString text = textOf(s);
        const bool named = !text.isEmpty() && text != key;
        if (s->type == QLatin1String("stateContainer")) {
            if (named)
                out.append(QStringLiteral("%1state \"%2\" as %3").arg(indent, escaped(text), key));
            out.append(QStringLiteral("%1state %2 {").arg(indent, key));
            emitScope(s, depth + 1);
            out.append(indent + QLatin1Char('}'));
            return;
        }
        if (s->type == QLatin1String("choice") || s->type == QLatin1String("fork") ||
            s->type == QLatin1String("join")) {
            if (s->type == QLatin1String("choice") && named)
                out.append(QStringLiteral("%1state \"%2\" as %3").arg(indent, escaped(text), key));
            out.append(QStringLiteral("%1state %2 <<%3>>").arg(indent, key, s->type));
            return;
        }
        const auto *state = dynamic_cast<const StateShape *>(s);
        const QStringList desc = state ? state->desc : QStringList();
        if (named)
            out.append(QStringLiteral("%1state \"%2\" as %3").arg(indent, escaped(text), key));
        for (const QString &d : desc)
            out.append(QStringLiteral("%1%2 : %3").arg(indent, key, escaped(d)));
        // A state that nothing else mentions is declared, or it would be lost.
        if (!named && desc.isEmpty() && !linked(s))
            out.append(QStringLiteral("%1state %2").arg(indent, key));
    };
    emitScope = [&](const Shape *parent, int depth) {
        for (const auto &s : shapes) {
            if (parentOf(s.get()) == parent)
                emitShape(s.get(), depth);
        }
        const QString indent(depth * 2, QLatin1Char(' '));
        for (const auto &l : board.links()) {
            const std::optional<const Shape *> scope = scopeOf(*l);
            if (!scope || *scope != parent)
                continue;
            const QString from = keys.value(l->from), to = keys.value(l->to);
            if (from.isEmpty() || to.isEmpty())
                continue;
            out.append(
                QStringLiteral("%1%2 --> %3%4")
                    .arg(indent, from, to, l->label.isEmpty() ? QString() : QStringLiteral(" : ") + escaped(l->label)));
        }
    };
    emitScope(nullptr, 1);
    return out;
}

// ---------------------------------------------------------------------------
// Mermaid

// Grammar subset checked against stateDiagram.jison (case-insensitive):
// `[*]` ends, `A --> B : label`, `state "desc" as id`, `id : description`,
// `<<choice|fork|join>>` and nested composites. Notes and styling are
// ignored.
class MermaidState : public Codec {
public:
    QString id() const override { return QStringLiteral("mermaid-state"); }
    QString language() const override { return QStringLiteral("mermaid"); }
    QString title() const override { return QStringLiteral("State diagram"); }
    const Family &family() const override { return families::states(); }

    static const QRegularExpression &header()
    {
        static const QRegularExpression pattern = caseless(QStringLiteral("^stateDiagram(-v2)?\\b"));
        return pattern;
    }

    bool matches(const QString &source) const override
    {
        // Comments (a title, the layout) may come first.
        for (const QString &line : mermaidLines(source))
            return header().match(line).hasMatch();
        return false;
    }

    bool parse(const QString &source, Model &model, QString *) const override
    {
        static const QRegularExpression skip = caseless(
            QStringLiteral("^(?:classDef\\b|class\\b|style\\b|click\\b|hide\\b|scale\\b|accTitle|accDescr|--$)"));
        static const QRegularExpression endNote = caseless(QStringLiteral("^end note$"));
        static const QRegularExpression note = caseless(QStringLiteral("^note\\b"));
        static const QRegularExpression direction = caseless(QStringLiteral("^direction\\s+(TB|BT|RL|LR)"));
        static const QRegularExpression composite =
            caseless(QStringLiteral("^state\\s+(?:\"([^\"]*)\"\\s+as\\s+([^\\s:{]+)|([^\\s:{]+))\\s*\\{\\s*(\\})?$"));
        static const QRegularExpression cssClass(QStringLiteral(":::[\\w-]+"));
        static const QRegularExpression transition(
            QStringLiteral("^(\\[\\*\\]|[^\\s:{]+)\\s*-->\\s*(\\[\\*\\]|[^\\s:{]+)\\s*(?::\\s*(.*))?$"));
        static const QRegularExpression described =
            caseless(QStringLiteral("^state\\s+\"([^\"]*)\"\\s+as\\s+([^\\s:{]+)"));
        static const QRegularExpression special =
            caseless(QStringLiteral("^state\\s+([^\\s:{]+)\\s*(?:<<(choice|fork|join)>>)?$"));
        static const QRegularExpression descLine(QStringLiteral("^([^\\s:{]+)\\s*:\\s*(.+)$"));

        StateModel states;
        QString rankdir = QStringLiteral("TB");
        bool inNote = false;
        QStringList scopes;
        const auto scope = [&scopes] { return scopes.isEmpty() ? QString() : scopes.back(); };

        for (QString stmt : mermaidLines(source)) {
            if (header().match(stmt).hasMatch())
                continue;
            if (inNote) {
                if (endNote.match(stmt).hasMatch())
                    inNote = false;
                continue;
            }
            if (note.match(stmt).hasMatch()) {
                if (!stmt.contains(QLatin1Char(':')))
                    inNote = true;
                continue;
            }
            if (stmt == QLatin1String("}")) {
                if (!scopes.isEmpty())
                    scopes.removeLast();
                continue;
            }
            if (const QRegularExpressionMatch m = direction.match(stmt); m.hasMatch()) {
                rankdir = m.captured(1).toUpper();
                continue;
            }
            if (const QRegularExpressionMatch m = composite.match(stmt); m.hasMatch()) {
                const QString sourceKey = m.hasCaptured(2) ? m.captured(2) : m.captured(3);
                const std::optional<QString> label =
                    m.hasCaptured(1) ? std::optional<QString>(decodeLabel(m.captured(1))) : std::nullopt;
                const QString key = states.container(sourceKey, scope(), label).key;
                if (!m.hasCaptured(4))
                    scopes.append(key);
                continue;
            }
            if (skip.match(stmt).hasMatch())
                continue;
            stmt = stmt.remove(cssClass).trimmed();
            if (stmt.isEmpty())
                continue;
            if (const QRegularExpressionMatch m = transition.match(stmt); m.hasMatch()) {
                states.transition(m.captured(1), m.captured(2), decodeLabel(m.captured(3)), scope());
                continue;
            }
            if (const QRegularExpressionMatch m = described.match(stmt); m.hasMatch()) {
                states.state(m.captured(2), scope()).text = decodeLabel(m.captured(1));
                continue;
            }
            if (const QRegularExpressionMatch m = special.match(stmt); m.hasMatch()) {
                StateModel::Node &node = states.state(m.captured(1), scope());
                if (m.hasCaptured(2))
                    node.type = m.captured(2).toLower();
                continue;
            }
            if (const QRegularExpressionMatch m = descLine.match(stmt); m.hasMatch()) {
                states.state(m.captured(1), scope()).desc.append(decodeLabel(m.captured(2).trimmed()));
                continue;
            }
            model.warnings.append(QStringLiteral("Not understood: %1").arg(stmt));
        }
        states.result(model, rankdir);
        return true;
    }

    QString serialize(const Board &board, const QJsonObject &meta) const override
    {
        QStringList out{QStringLiteral("stateDiagram-v2")};
        const QString rankdir = meta.value(QStringLiteral("rankdir")).toString();
        if (!rankdir.isEmpty() && rankdir != QLatin1String("TB"))
            out.append(QStringLiteral("  direction ") + rankdir);
        return serializeStates(board, out).join(QLatin1Char('\n'));
    }

    QString blank() const override
    {
        return QStringLiteral(
            "stateDiagram-v2\n  [*] --> Draft\n  Draft --> Validated : validate\n  Validated --> [*]");
    }
};

// ---------------------------------------------------------------------------
// PlantUML

// Checked against plantuml.com/state-diagram, close to Mermaid's (which
// descends from it): `->` and the reversed `<--` too, `X as "label"`, and
// arrows with a direction or a color, reduced to `-->`.
class PlantumlState : public Codec {
public:
    QString id() const override { return QStringLiteral("plantuml-state"); }
    QString language() const override { return QStringLiteral("plantuml"); }
    QString title() const override { return QStringLiteral("State diagram"); }
    const Family &family() const override { return families::states(); }

    bool matches(const QString &source) const override
    {
        static const QRegularExpression state = caseless(QStringLiteral("^state\\b"));
        for (const QString &line : plantumlLines(source)) {
            if (line.contains(QStringLiteral("[*]")) || state.match(line).hasMatch())
                return true;
        }
        return false;
    }

    // `-up->`, `-[#DD00AA]->`, `-left[#yellow]->`… come down to `-->`.
    static QString canonicalArrows(QString stmt)
    {
        static const QRegularExpression styled =
            caseless(QStringLiteral("-(?:left|right|up|down|le?|ri?|u|d)?\\[[^\\]]*\\]-?"));
        static const QRegularExpression directed = caseless(QStringLiteral("-(?:left|right|up|down|le?|ri?|u|d)-"));
        static const QRegularExpression longer(QStringLiteral("-{3,}"));
        stmt.replace(styled, QStringLiteral("--"));
        stmt.replace(directed, QStringLiteral("--"));
        stmt.replace(longer, QStringLiteral("--"));
        return stmt;
    }

    bool parse(const QString &source, Model &model, QString *) const override
    {
        static const QRegularExpression skip = caseless(QStringLiteral("^(?:note\\b|end\\s+note|--$|\\|\\|$)"));
        static const QRegularExpression endNote = caseless(QStringLiteral("^end\\s+note$"));
        static const QRegularExpression note = caseless(QStringLiteral("^note\\b"));
        static const QRegularExpression color(QStringLiteral("#\\S+$"));
        static const QRegularExpression composite =
            caseless(QStringLiteral("^state\\s+(?:\"([^\"]*)\"\\s+as\\s+([\\w.]+)|([\\w.]+)\\s+as\\s+\"([^\"]*)\"|(["
                                    "\\w.]+))\\s*\\{\\s*(\\})?$"));
        static const QRegularExpression transition(
            QStringLiteral("^(\\[\\*\\]|[\\w.]+)\\s*(<--?|--?>?-?>)\\s*(\\[\\*\\]|[\\w.]+)\\s*(?::\\s*(.*))?$"));
        static const QRegularExpression described =
            caseless(QStringLiteral("^state\\s+(?:\"([^\"]+)\"\\s+as\\s+([\\w.]+)|([\\w.]+)\\s+as\\s+\"([^\"]+)\")"));
        static const QRegularExpression special =
            caseless(QStringLiteral("^state\\s+([\\w.]+)\\s*(?:<<\\s*(\\w+\\*?)\\s*>>)?\\s*(?::\\s*(.*))?$"));
        static const QRegularExpression pseudo = caseless(QStringLiteral("^(choice|fork|join)$"));
        static const QRegularExpression descLine(QStringLiteral("^([\\w.]+)\\s*:\\s*(.+)$"));

        StateModel states;
        bool inNote = false;
        QStringList scopes;
        const auto scope = [&scopes] { return scopes.isEmpty() ? QString() : scopes.back(); };

        for (QString stmt : plantumlLines(source)) {
            if (inNote) {
                if (endNote.match(stmt).hasMatch())
                    inNote = false;
                continue;
            }
            if (note.match(stmt).hasMatch()) {
                if (!stmt.contains(QLatin1Char(':')))
                    inNote = true;
                continue;
            }
            if (stmt == QLatin1String("}")) {
                if (!scopes.isEmpty())
                    scopes.removeLast();
                continue;
            }
            stmt = canonicalArrows(stmt).remove(color).trimmed();
            if (const QRegularExpressionMatch m = composite.match(stmt); m.hasMatch()) {
                const QString sourceKey = m.hasCaptured(2)   ? m.captured(2)
                                          : m.hasCaptured(3) ? m.captured(3)
                                                             : m.captured(5);
                std::optional<QString> label;
                if (m.hasCaptured(1))
                    label = plantumlText(m.captured(1));
                else if (m.hasCaptured(4))
                    label = plantumlText(m.captured(4));
                const QString key = states.container(sourceKey, scope(), label).key;
                if (!m.hasCaptured(6))
                    scopes.append(key);
                continue;
            }
            if (stmt.isEmpty() || skip.match(stmt).hasMatch())
                continue;
            if (const QRegularExpressionMatch m = transition.match(stmt); m.hasMatch()) {
                const QString label = plantumlText(m.captured(4));
                if (m.captured(2).startsWith(QLatin1Char('<')))
                    states.transition(m.captured(3), m.captured(1), label, scope());
                else
                    states.transition(m.captured(1), m.captured(3), label, scope());
                continue;
            }
            if (const QRegularExpressionMatch m = described.match(stmt); m.hasMatch()) {
                StateModel::Node &node = states.state(m.hasCaptured(2) ? m.captured(2) : m.captured(3), scope());
                node.text = plantumlText(m.hasCaptured(1) ? m.captured(1) : m.captured(4));
                continue;
            }
            if (const QRegularExpressionMatch m = special.match(stmt); m.hasMatch()) {
                StateModel::Node &node = states.state(m.captured(1), scope());
                if (pseudo.match(m.captured(2)).hasMatch())
                    node.type = m.captured(2).toLower();
                if (!m.captured(3).isEmpty())
                    node.desc.append(plantumlText(m.captured(3).trimmed()));
                continue;
            }
            if (const QRegularExpressionMatch m = descLine.match(stmt); m.hasMatch()) {
                states.state(m.captured(1), scope()).desc.append(plantumlText(m.captured(2).trimmed()));
                continue;
            }
            model.warnings.append(QStringLiteral("Not understood: %1").arg(stmt));
        }
        states.result(model, QStringLiteral("TB"));
        return true;
    }

    QString serialize(const Board &board, const QJsonObject &) const override
    {
        QStringList out{QStringLiteral("@startuml")};
        out += serializeStates(board, {});
        out.append(QStringLiteral("@enduml"));
        return out.join(QLatin1Char('\n'));
    }

    QString blank() const override
    {
        return QStringLiteral("@startuml\n[*] --> Draft\nDraft --> Validated : validate\nValidated --> [*]\n@enduml");
    }
};

} // namespace

} // namespace diagram::states

namespace diagram::families {

QList<const Codec *> stateCodecs()
{
    static const states::MermaidState mermaid;
    static const states::PlantumlState plantuml;
    return {&mermaid, &plantuml};
}

} // namespace diagram::families
