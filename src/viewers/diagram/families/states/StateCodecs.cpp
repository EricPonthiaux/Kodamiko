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

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <utility>

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

QString escaped(QString value)
{
    value.replace(QLatin1Char('"'), QLatin1Char('\''));
    value.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return value;
}

bool isEndpoint(const Shape *s)
{
    return s->type == QLatin1String("initial") || s->type == QLatin1String("final");
}

bool isPseudoState(const Shape *s)
{
    return s->type == QLatin1String("choice") || s->type == QLatin1String("fork") || s->type == QLatin1String("join");
}

QString textOf(const Shape *s)
{
    const auto *element = dynamic_cast<const StateElement *>(s);
    return element ? element->sourceText() : s->displayText();
}

QString indentOf(int depth)
{
    return {qsizetype(depth) * 2, QLatin1Char(' ')};
}

// Serialization shared by both notations: declarations and transitions are
// written inside the composite that owns them; initial and final states
// become `[*]`.
class StateWriter {
public:
    explicit StateWriter(const Board &board)
        : board_(board)
    {
        assignKeys();
    }

    // Appends the top level, and through it every composite, to `out`.
    QStringList write(QStringList out)
    {
        out_ = std::move(out);
        writeScope(nullptr, 1);
        return std::move(out_);
    }

private:
    const Shape *parentOf(const Shape *s) const
    {
        return s->parentBoundary.isEmpty() ? nullptr : board_.shapeByReference(s->parentBoundary);
    }

    // The id the source gave it in its scope, while the board key is still
    // the one that id produces; a copy (a key of its own) or an element
    // created on the board writes its key, scope removed.
    QString preferredKey(const Shape *s) const
    {
        const auto *element = dynamic_cast<const StateElement *>(s);
        QString sourceKey = element ? element->sourceKey : QString();
        const bool isKeyFromSource = s->key.isEmpty() || s->key == StateModel::scopedKey(sourceKey, s->parentBoundary);
        if (!sourceKey.isEmpty() && isKeyFromSource)
            return sourceKey;
        const QString scope = s->parentBoundary + QLatin1Char('.');
        const bool isScoped = parentOf(s) && s->key.startsWith(scope) && s->key.size() > scope.size();
        return isScoped ? s->key.mid(scope.size()) : s->key;
    }

    void assignKeys()
    {
        QSet<QString> used;
        for (const auto &s : board_.shapes()) {
            if (isEndpoint(s.get()))
                keys_.insert(s->id, QStringLiteral("[*]"));
            else
                keys_.insert(s->id, keyFor(preferredKey(s.get()), textOf(s.get()), used));
        }
    }

    // Mermaid forbids a transition between two composites' interiors: one
    // made on the board anyway goes to the top level rather than being lost.
    std::optional<const Shape *> scopeOf(const Link &l) const
    {
        const Shape *a = board_.shape(l.from);
        const Shape *b = board_.shape(l.to);
        if (!a || !b)
            return std::nullopt;
        const Shape *pa = parentOf(a);
        const Shape *pb = parentOf(b);
        return pa == pb ? pa : nullptr;
    }

    bool isLinked(const Shape *s) const
    {
        return std::ranges::any_of(board_.links(), [s](const auto &l) { return l->from == s->id || l->to == s->id; });
    }

    void writeScope(const Shape *parent, int depth)
    {
        for (const auto &s : board_.shapes()) {
            if (parentOf(s.get()) == parent)
                writeShape(s.get(), depth);
        }
        for (const auto &l : board_.links()) {
            const std::optional<const Shape *> scope = scopeOf(*l);
            if (scope && *scope == parent)
                writeTransition(*l, depth);
        }
    }

    void writeTransition(const Link &l, int depth)
    {
        const QString from = keys_.value(l.from);
        const QString to = keys_.value(l.to);
        if (from.isEmpty() || to.isEmpty())
            return;
        const QString label = l.label.isEmpty() ? QString() : QStringLiteral(" : ") + escaped(l.label);
        out_.append(QStringLiteral("%1%2 --> %3%4").arg(indentOf(depth), from, to, label));
    }

    void writeShape(const Shape *s, int depth)
    {
        if (isEndpoint(s))
            return;
        if (s->type == QLatin1String("stateContainer"))
            writeContainer(s, depth);
        else if (isPseudoState(s))
            writePseudoState(s, depth);
        else
            writeState(s, depth);
    }

    // The `state "text" as key` line of an element whose text is not its key.
    bool writeName(const Shape *s, int depth)
    {
        const QString key = keys_.value(s->id);
        const QString text = textOf(s);
        const bool named = !text.isEmpty() && text != key;
        if (named)
            out_.append(QStringLiteral("%1state \"%2\" as %3").arg(indentOf(depth), escaped(text), key));
        return named;
    }

    void writeContainer(const Shape *s, int depth)
    {
        const QString indent = indentOf(depth);
        writeName(s, depth);
        out_.append(QStringLiteral("%1state %2 {").arg(indent, keys_.value(s->id)));
        writeScope(s, depth + 1);
        out_.append(indent + QLatin1Char('}'));
    }

    void writePseudoState(const Shape *s, int depth)
    {
        // Only a choice carries a condition; a fork or a join has no text.
        if (s->type == QLatin1String("choice"))
            writeName(s, depth);
        out_.append(QStringLiteral("%1state %2 <<%3>>").arg(indentOf(depth), keys_.value(s->id), s->type));
    }

    void writeState(const Shape *s, int depth)
    {
        const QString indent = indentOf(depth);
        const QString key = keys_.value(s->id);
        const auto *state = dynamic_cast<const StateShape *>(s);
        const QStringList desc = state ? state->desc : QStringList();
        const bool named = writeName(s, depth);
        for (const QString &d : desc)
            out_.append(QStringLiteral("%1%2 : %3").arg(indent, key, escaped(d)));
        // A state that nothing else mentions is declared, or it would be lost.
        if (!named && desc.isEmpty() && !isLinked(s))
            out_.append(QStringLiteral("%1state %2").arg(indent, key));
    }

    const Board &board_;
    QHash<QString, QString> keys_; // shape id → the key the source writes
    QStringList out_;
};

QStringList serializeStates(const Board &board, QStringList out)
{
    return StateWriter(board).write(std::move(out));
}

// The reading state both notations share from one line to the next: the
// note being skipped and the composites open around the line.
class LineContext {
public:
    // Notes are skipped: a one-liner (`note … : text`), or a block up to its
    // end. True when the statement belongs to a note.
    bool consumesNote(const QString &stmt, const QRegularExpression &note, const QRegularExpression &endNote)
    {
        if (inNote_) {
            if (endNote.match(stmt).hasMatch())
                inNote_ = false;
            return true;
        }
        if (!note.match(stmt).hasMatch())
            return false;
        if (!stmt.contains(QLatin1Char(':')))
            inNote_ = true;
        return true;
    }

    // True when the statement closes the current composite.
    bool closesComposite(const QString &stmt)
    {
        if (stmt != QLatin1String("}"))
            return false;
        if (!scopes_.isEmpty())
            scopes_.removeLast();
        return true;
    }

    void openComposite(const QString &key) { scopes_.append(key); }

    // The composite the current line is in; empty at the top level.
    QString scope() const { return scopes_.isEmpty() ? QString() : scopes_.back(); }

private:
    bool inNote_ = false;
    QStringList scopes_;
};

// The first of these groups that took part in the match (the alternatives of
// one pattern), or an empty text.
QString firstCaptured(const QRegularExpressionMatch &m, std::initializer_list<int> groups)
{
    for (const int group : groups) {
        if (m.hasCaptured(group))
            return m.captured(group);
    }
    return {};
}

// ---------------------------------------------------------------------------
// Mermaid

const QRegularExpression &mermaidHeader()
{
    static const QRegularExpression pattern = caseless(QStringLiteral("^stateDiagram(-v2)?\\b"));
    return pattern;
}

// Grammar subset checked against stateDiagram.jison (case-insensitive):
// `[*]` ends, `A --> B : label`, `state "desc" as id`, `id : description`,
// `<<choice|fork|join>>` and nested composites. Notes and styling are
// ignored.
class MermaidStateReader {
public:
    void read(const QString &source, Model &model)
    {
        for (const QString &stmt : mermaidLines(source))
            readStatement(stmt, model);
        states_.result(model, rankdir_);
    }

private:
    void readStatement(QString stmt, Model &model)
    {
        static const QRegularExpression skip = caseless(
            QStringLiteral("^(?:classDef\\b|class\\b|style\\b|click\\b|hide\\b|scale\\b|accTitle|accDescr|--$)"));
        static const QRegularExpression endNote = caseless(QStringLiteral("^end note$"));
        static const QRegularExpression note = caseless(QStringLiteral("^note\\b"));
        static const QRegularExpression cssClass(QStringLiteral(":::[\\w-]+"));
        if (mermaidHeader().match(stmt).hasMatch())
            return;
        if (context_.consumesNote(stmt, note, endNote) || context_.closesComposite(stmt))
            return;
        if (readDirection(stmt) || readComposite(stmt))
            return;
        if (skip.match(stmt).hasMatch())
            return;
        stmt = stmt.remove(cssClass).trimmed();
        if (stmt.isEmpty() || readDeclaration(stmt))
            return;
        model.warnings.append(QStringLiteral("Not understood: %1").arg(stmt));
    }

    bool readDirection(const QString &stmt)
    {
        static const QRegularExpression direction = caseless(QStringLiteral("^direction\\s+(TB|BT|RL|LR)"));
        const QRegularExpressionMatch m = direction.match(stmt);
        if (!m.hasMatch())
            return false;
        rankdir_ = m.captured(1).toUpper();
        return true;
    }

    bool readComposite(const QString &stmt)
    {
        static const QRegularExpression composite =
            caseless(QStringLiteral("^state\\s+(?:\"([^\"]*)\"\\s+as\\s+([^\\s:{]+)|([^\\s:{]+))\\s*\\{\\s*(\\})?$"));
        const QRegularExpressionMatch m = composite.match(stmt);
        if (!m.hasMatch())
            return false;
        const QString sourceKey = firstCaptured(m, {2, 3});
        const std::optional<QString> label =
            m.hasCaptured(1) ? std::optional<QString>(decodeLabel(m.captured(1))) : std::nullopt;
        const QString key = states_.container(sourceKey, context_.scope(), label).key;
        // `state X {}` opens and closes on the same line.
        if (!m.hasCaptured(4))
            context_.openComposite(key);
        return true;
    }

    // Transitions, named states, pseudo-states and description lines.
    bool readDeclaration(const QString &stmt)
    {
        static const QRegularExpression transition(
            QStringLiteral("^(\\[\\*\\]|[^\\s:{]+)\\s*-->\\s*(\\[\\*\\]|[^\\s:{]+)\\s*(?::\\s*(.*))?$"));
        static const QRegularExpression described =
            caseless(QStringLiteral("^state\\s+\"([^\"]*)\"\\s+as\\s+([^\\s:{]+)"));
        static const QRegularExpression special =
            caseless(QStringLiteral("^state\\s+([^\\s:{]+)\\s*(?:<<(choice|fork|join)>>)?$"));
        static const QRegularExpression descLine(QStringLiteral("^([^\\s:{]+)\\s*:\\s*(.+)$"));
        const QString scope = context_.scope();
        if (const QRegularExpressionMatch m = transition.match(stmt); m.hasMatch()) {
            states_.transition(m.captured(1), m.captured(2), decodeLabel(m.captured(3)), scope);
            return true;
        }
        if (const QRegularExpressionMatch m = described.match(stmt); m.hasMatch()) {
            states_.state(m.captured(2), scope).text = decodeLabel(m.captured(1));
            return true;
        }
        if (const QRegularExpressionMatch m = special.match(stmt); m.hasMatch()) {
            StateModel::Node &node = states_.state(m.captured(1), scope);
            if (m.hasCaptured(2))
                node.type = m.captured(2).toLower();
            return true;
        }
        if (const QRegularExpressionMatch m = descLine.match(stmt); m.hasMatch()) {
            states_.state(m.captured(1), scope).desc.append(decodeLabel(m.captured(2).trimmed()));
            return true;
        }
        return false;
    }

    StateModel states_;
    LineContext context_;
    QString rankdir_ = QStringLiteral("TB");
};

class MermaidState : public Codec {
public:
    QString id() const override { return QStringLiteral("mermaid-state"); }

    QString language() const override { return QStringLiteral("mermaid"); }

    QString title() const override { return QStringLiteral("State diagram"); }

    const Family &family() const override { return families::states(); }

    bool matches(const QString &source) const override
    {
        // Comments (a title, the layout) may come first.
        for (const QString &line : mermaidLines(source))
            return mermaidHeader().match(line).hasMatch();
        return false;
    }

    bool parse(const QString &source, Model &model, QString *) const override
    {
        MermaidStateReader().read(source, model);
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

// `-up->`, `-[#DD00AA]->`, `-left[#yellow]->`… come down to `-->`.
QString canonicalArrows(QString stmt)
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

// Checked against plantuml.com/state-diagram, close to Mermaid's (which
// descends from it): `->` and the reversed `<--` too, `X as "label"`, and
// arrows with a direction or a color, reduced to `-->`.
class PlantumlStateReader {
public:
    void read(const QString &source, Model &model)
    {
        for (const QString &stmt : plantumlLines(source))
            readStatement(stmt, model);
        states_.result(model, QStringLiteral("TB"));
    }

private:
    void readStatement(const QString &line, Model &model)
    {
        static const QRegularExpression skip = caseless(QStringLiteral("^(?:note\\b|end\\s+note|--$|\\|\\|$)"));
        static const QRegularExpression endNote = caseless(QStringLiteral("^end\\s+note$"));
        static const QRegularExpression note = caseless(QStringLiteral("^note\\b"));
        static const QRegularExpression color(QStringLiteral("#\\S+$"));
        if (context_.consumesNote(line, note, endNote) || context_.closesComposite(line))
            return;
        const QString stmt = canonicalArrows(line).remove(color).trimmed();
        if (readComposite(stmt))
            return;
        if (stmt.isEmpty() || skip.match(stmt).hasMatch())
            return;
        if (readTransition(stmt) || readDeclaration(stmt))
            return;
        model.warnings.append(QStringLiteral("Not understood: %1").arg(stmt));
    }

    bool readComposite(const QString &stmt)
    {
        static const QRegularExpression composite =
            caseless(QStringLiteral("^state\\s+(?:\"([^\"]*)\"\\s+as\\s+([\\w.]+)|([\\w.]+)\\s+as\\s+\"([^\"]*)\"|(["
                                    "\\w.]+))\\s*\\{\\s*(\\})?$"));
        const QRegularExpressionMatch m = composite.match(stmt);
        if (!m.hasMatch())
            return false;
        const QString sourceKey = firstCaptured(m, {2, 3, 5});
        std::optional<QString> label;
        if (m.hasCaptured(1) || m.hasCaptured(4))
            label = plantumlText(firstCaptured(m, {1, 4}));
        const QString key = states_.container(sourceKey, context_.scope(), label).key;
        // `state X {}` opens and closes on the same line.
        if (!m.hasCaptured(6))
            context_.openComposite(key);
        return true;
    }

    bool readTransition(const QString &stmt)
    {
        static const QRegularExpression transition(
            QStringLiteral("^(\\[\\*\\]|[\\w.]+)\\s*(<--?|--?>?-?>)\\s*(\\[\\*\\]|[\\w.]+)\\s*(?::\\s*(.*))?$"));
        const QRegularExpressionMatch m = transition.match(stmt);
        if (!m.hasMatch())
            return false;
        const QString label = plantumlText(m.captured(4));
        const bool isReversed = m.captured(2).startsWith(QLatin1Char('<'));
        if (isReversed)
            states_.transition(m.captured(3), m.captured(1), label, context_.scope());
        else
            states_.transition(m.captured(1), m.captured(3), label, context_.scope());
        return true;
    }

    // Named states, pseudo-states and description lines.
    bool readDeclaration(const QString &stmt)
    {
        static const QRegularExpression described =
            caseless(QStringLiteral("^state\\s+(?:\"([^\"]+)\"\\s+as\\s+([\\w.]+)|([\\w.]+)\\s+as\\s+\"([^\"]+)\")"));
        static const QRegularExpression special =
            caseless(QStringLiteral("^state\\s+([\\w.]+)\\s*(?:<<\\s*(\\w+\\*?)\\s*>>)?\\s*(?::\\s*(.*))?$"));
        static const QRegularExpression pseudo = caseless(QStringLiteral("^(choice|fork|join)$"));
        static const QRegularExpression descLine(QStringLiteral("^([\\w.]+)\\s*:\\s*(.+)$"));
        const QString scope = context_.scope();
        if (const QRegularExpressionMatch m = described.match(stmt); m.hasMatch()) {
            StateModel::Node &node = states_.state(firstCaptured(m, {2, 3}), scope);
            node.text = plantumlText(firstCaptured(m, {1, 4}));
            return true;
        }
        if (const QRegularExpressionMatch m = special.match(stmt); m.hasMatch()) {
            StateModel::Node &node = states_.state(m.captured(1), scope);
            if (pseudo.match(m.captured(2)).hasMatch())
                node.type = m.captured(2).toLower();
            if (!m.captured(3).isEmpty())
                node.desc.append(plantumlText(m.captured(3).trimmed()));
            return true;
        }
        if (const QRegularExpressionMatch m = descLine.match(stmt); m.hasMatch()) {
            states_.state(m.captured(1), scope).desc.append(plantumlText(m.captured(2).trimmed()));
            return true;
        }
        return false;
    }

    StateModel states_;
    LineContext context_;
};

class PlantumlState : public Codec {
public:
    QString id() const override { return QStringLiteral("plantuml-state"); }

    QString language() const override { return QStringLiteral("plantuml"); }

    QString title() const override { return QStringLiteral("State diagram"); }

    const Family &family() const override { return families::states(); }

    bool matches(const QString &source) const override
    {
        static const QRegularExpression state = caseless(QStringLiteral("^state\\b"));
        return std::ranges::any_of(plantumlLines(source), [](const QString &line) {
            return line.contains(QStringLiteral("[*]")) || state.match(line).hasMatch();
        });
    }

    bool parse(const QString &source, Model &model, QString *) const override
    {
        PlantumlStateReader().read(source, model);
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
