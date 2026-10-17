// Mermaid sequence diagrams (sidecar's fence/mermaid-sequence.js) and
// PlantUML ones (fence/plantuml-sequence.js).

#include "Sequence.h"

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/SourceText.h"
#include "viewers/diagram/families/Families.h"

#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>

namespace diagram::sequence {

namespace {

using namespace diagram::source;

// Participants as the sources place them, before the family's layout.
constexpr int participantGap = 220;

// A participant as a source declares it, keyed by its source name.
struct Node {
    QString key;
    QString kind = QStringLiteral("participant");
    QString text;
};

struct Nodes {
    QHash<QString, qsizetype> index;
    QList<Node> list;

    Node &get(const QString &key, const QString &kind)
    {
        qsizetype at = index.value(key, -1);
        if (at < 0) {
            at = list.size();
            index.insert(key, at);
            list.append({key, kind, key});
        }
        return list[at];
    }

    void toModel(Model &model) const
    {
        for (qsizetype i = 0; i < list.size(); ++i) {
            const Node &node = list[i];
            model.nodes.append({QStringLiteral("participant"), node.key,
                                QJsonObject{{QStringLiteral("text"), node.text},
                                            {QStringLiteral("kind"), node.kind},
                                            {QStringLiteral("x"), int(i) * participantGap},
                                            {QStringLiteral("y"), 20}}});
        }
    }
};

QJsonArray fragmentsJson(const QList<Fragment> &stack)
{
    QJsonArray array;
    for (const Fragment &f : stack)
        array.append(f.toJson());
    return array;
}

// The board's participants left to right, and its messages in conversation
// order: what both serializers walk.
QList<const ParticipantShape *> participantsOf(const Board &board)
{
    QList<const ParticipantShape *> result;
    for (const auto &s : board.shapes()) {
        if (const auto *p = dynamic_cast<const ParticipantShape *>(s.get()))
            result.append(p);
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const ParticipantShape *a, const ParticipantShape *b) { return a->x < b->x; });
    return result;
}

QList<const MessageLink *> messagesOf(const Board &board)
{
    QList<const MessageLink *> result;
    for (const auto &l : board.links()) {
        if (const auto *m = dynamic_cast<const MessageLink *>(l.get()))
            result.append(m);
    }
    std::stable_sort(result.begin(), result.end(), [](const MessageLink *a, const MessageLink *b) {
        return a->order.value_or(0) < b->order.value_or(0);
    });
    return result;
}

// ---------------------------------------------------------------------------
// Mermaid

// Grammar subset checked against sequenceDiagram.jison (case-insensitive):
// participant/actor declarations with alias, the message arrows, activation
// marks. Blocks (loop/alt/opt/par…) and their branches are kept as
// fragments. Notes, autonumber and styling are not.

struct Arrow {
    const char *token;
    const char *line;
    const char *head;
    const char *startHead;
};
// Longest first, so that `-->>` never reads as `-->` + `>`.
const Arrow arrows[] = {
    {"<<-->>", "dashed", "arrow", "arrow"}, {"<<->>", "solid", "arrow", "arrow"}, {"-->>", "dashed", "arrow", "none"},
    {"->>", "solid", "arrow", "none"},      {"--x", "dashed", "cross", "none"},   {"-x", "solid", "cross", "none"},
    {"--)", "dashed", "open", "none"},      {"-)", "solid", "open", "none"},      {"-->", "dashed", "none", "none"},
    {"->", "solid", "none", "none"},
};

const QRegularExpression &mermaidMessage()
{
    static const QRegularExpression pattern = [] {
        QStringList alternatives;
        for (const Arrow &arrow : arrows)
            alternatives.append(QRegularExpression::escape(QLatin1String(arrow.token)));
        return QRegularExpression(QStringLiteral("^(.+?)\\s*(") + alternatives.join(QLatin1Char('|'))
                                  + QStringLiteral(")\\s*([+-]?)\\s*([^:]+?)\\s*(?::\\s*(.*))?$"));
    }();
    return pattern;
}

const QRegularExpression &mermaidHeader()
{
    static const QRegularExpression pattern(QStringLiteral("^sequenceDiagram\\b"), QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// Presentation only: nothing is lost by leaving it out.
const QRegularExpression &mermaidPresentation()
{
    static const QRegularExpression pattern(
        QStringLiteral("^(?:autonumber\\b|title\\b|accTitle|accDescr|links\\b|link\\b|properties\\b|details\\b)"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// Read but not kept (notes, standalone activations, boxes): reported.
const QRegularExpression &mermaidDropped()
{
    static const QRegularExpression pattern(
        QStringLiteral("^(?:activate\\b|deactivate\\b|destroy\\b|box\\b|note\\b)"), QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

QString mermaidArrow(const MessageLink &l)
{
    const bool solid = l.line != QLatin1String("dashed");
    if (l.startHead == QLatin1String("arrow") && l.head == QLatin1String("arrow"))
        return solid ? QStringLiteral("<<->>") : QStringLiteral("<<-->>");
    if (l.head == QLatin1String("arrow"))
        return solid ? QStringLiteral("->>") : QStringLiteral("-->>");
    if (l.head == QLatin1String("cross"))
        return solid ? QStringLiteral("-x") : QStringLiteral("--x");
    if (l.head == QLatin1String("open"))
        return solid ? QStringLiteral("-)") : QStringLiteral("--)");
    return solid ? QStringLiteral("->") : QStringLiteral("-->");
}

// A text in a Mermaid statement: line breaks as <br/>, and `;` (which
// separates statements) as its entity.
QString mermaidText(const QString &text)
{
    QString result = text;
    result.replace(QLatin1Char(';'), QStringLiteral("#59;"));
    result.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
    return result;
}

class MermaidSequence : public Codec {
public:
    QString id() const override { return QStringLiteral("mermaid-sequence"); }
    QString language() const override { return QStringLiteral("mermaid"); }
    QString title() const override { return QStringLiteral("Sequence diagram"); }
    const Family &family() const override { return families::sequence(); }
    // Participants on a row; messages derive their own lanes.
    bool arrangeWithGraphviz() const override { return false; }

    bool matches(const QString &source) const override
    {
        // Comments (the layout, a title) may come first.
        for (const QString &line : mermaidLines(source))
            return mermaidHeader().match(line).hasMatch();
        return false;
    }

    bool parse(const QString &source, Model &model, QString *) const override
    {
        Nodes nodes;
        int order = 0;
        int fragmentSeq = 0;
        QList<Fragment> stack;

        // `;` separates Mermaid statements, but it also ends the entities
        // (&amp; #quot;): they are hidden before splitting.
        static const QRegularExpression entity(QStringLiteral("([&#]#?\\w{1,8});"));
        QStringList statements;
        for (const QString &line : mermaidLines(source)) {
            QString masked = line;
            masked.replace(entity, QStringLiteral("\\1\x01"));
            for (QString part : masked.split(QLatin1Char(';'))) {
                part.replace(QLatin1Char('\x01'), QLatin1Char(';'));
                part = part.trimmed();
                if (!part.isEmpty())
                    statements.append(part);
            }
        }

        static const QRegularExpression declaration(
            QStringLiteral("^(?:create\\s+)?(participant|actor)\\s+(.+?)(?:\\s+as\\s+(.+))?$"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression open(QStringLiteral("^(loop|alt|opt|par|critical|break|rect)\\b\\s*(.*)$"),
                                             QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression branch(QStringLiteral("^(else|and|option)\\b\\s*(.*)$"),
                                               QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression end(QStringLiteral("^end\\b"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression wrap(QStringLiteral("^(?:wrap|nowrap):\\s*"), QRegularExpression::CaseInsensitiveOption);

        for (const QString &stmt : statements) {
            if (mermaidHeader().match(stmt).hasMatch())
                continue;
            if (const QRegularExpressionMatch m = declaration.match(stmt); m.hasMatch()) {
                const QString key = decodeLabel(m.captured(2)).trimmed();
                const QString kind = m.captured(1).toLower();
                Node &node = nodes.get(key, kind);
                if (m.hasCaptured(3) && !m.captured(3).isEmpty())
                    node.text = decodeLabel(m.captured(3).trimmed());
                if (kind == QLatin1String("actor"))
                    node.kind = kind;
                continue;
            }
            if (const QRegularExpressionMatch m = open.match(stmt); m.hasMatch()) {
                Fragment f;
                f.id = QStringLiteral("fragment-%1").arg(++fragmentSeq);
                f.kind = m.captured(1).toLower();
                f.label = decodeLabel(m.captured(2).trimmed());
                f.depth = int(stack.size());
                stack.append(f);
                continue;
            }
            if (const QRegularExpressionMatch m = branch.match(stmt); m.hasMatch() && !stack.isEmpty()) {
                stack.back().branchKeyword = m.captured(1).toLower();
                stack.back().branchLabel = decodeLabel(m.captured(2).trimmed());
                continue;
            }
            if (end.match(stmt).hasMatch()) {
                if (!stack.isEmpty())
                    stack.removeLast();
                continue;
            }
            if (mermaidPresentation().match(stmt).hasMatch())
                continue;
            const QRegularExpressionMatch msg = mermaidDropped().match(stmt).hasMatch()
                                                    ? QRegularExpressionMatch()
                                                    : mermaidMessage().match(stmt);
            if (!msg.hasMatch()) {
                model.warnings.append(QStringLiteral("Not understood: %1").arg(stmt));
                continue;
            }
            const QString token = msg.captured(2);
            const Arrow *arrow = std::find_if(std::begin(arrows), std::end(arrows),
                                              [&](const Arrow &a) { return token == QLatin1String(a.token); });
            const QString from = nodes.get(msg.captured(1).trimmed(), QStringLiteral("participant")).key;
            const QString to = nodes.get(msg.captured(4).trimmed(), QStringLiteral("participant")).key;
            QString text = msg.captured(5);
            text.remove(wrap);
            model.links.append({QStringLiteral("message"), from, to,
                                QJsonObject{{QStringLiteral("label"), decodeLabel(text.trimmed())},
                                            {QStringLiteral("line"), QLatin1String(arrow->line)},
                                            {QStringLiteral("head"), QLatin1String(arrow->head)},
                                            {QStringLiteral("startHead"), QLatin1String(arrow->startHead)},
                                            {QStringLiteral("activation"), msg.captured(3)},
                                            {QStringLiteral("fragments"), fragmentsJson(stack)},
                                            {QStringLiteral("order"), order++}}});
        }
        nodes.toModel(model);
        model.meta = QJsonObject{};
        return true;
    }

    QString serialize(const Board &board, const QJsonObject &) const override
    {
        QSet<QString> used;
        QHash<QString, QString> keys;
        QStringList out{QStringLiteral("sequenceDiagram")};
        // Participants left to right, as laid out on the board.
        for (const ParticipantShape *s : participantsOf(board)) {
            const QString key = keyFor(s->key, s->text, used);
            keys.insert(s->id, key);
            const QString kind = s->isActor() ? QStringLiteral("actor") : QStringLiteral("participant");
            out.append(!s->text.isEmpty() && s->text != key
                           ? QStringLiteral("  %1 %2 as %3").arg(kind, key, mermaidText(s->text))
                           : QStringLiteral("  %1 %2").arg(kind, key));
        }
        // Messages in conversation order, fragments opened and closed
        // around them.
        QList<Fragment> active;
        for (const MessageLink *l : messagesOf(board)) {
            const QString from = keys.value(l->from), to = keys.value(l->to);
            if (from.isEmpty() || to.isEmpty())
                continue;
            active = syncFragments(out, active, l->fragments, QStringLiteral("  "), true, mermaidText);
            out.append(QStringLiteral("  ") + from + mermaidArrow(*l) + l->activation + to + QStringLiteral(": ")
                       + mermaidText(l->label));
        }
        for (qsizetype i = active.size() - 1; i >= 0; --i)
            out.append(QStringLiteral("  end"));
        return out.join(QLatin1Char('\n'));
    }

    QString blank() const override
    {
        return QStringLiteral("sequenceDiagram\n  participant A as Actor\n  participant S as System\n"
                              "  A->>S: First interaction");
    }
};

// ---------------------------------------------------------------------------
// PlantUML

// Syntax checked against plantuml.com/sequence-diagram: participant kinds
// with `as` aliases (both orders), the arrow family (a double dash is
// dotted, `>`/`>>` heads, `x`/`o` decorations, reversed `<-`), `: text`
// messages, `++/--/**/!!` lifeline shortcuts. Groups (alt/loop/…) and
// participant kinds are kept; notes, standalone activations and styling are
// not.

const QString kinds = QStringLiteral("participant|actor|boundary|control|entity|database|collections|queue");

const QRegularExpression &plantumlDeclaration()
{
    static const QRegularExpression pattern(
        QStringLiteral("^(") + kinds
            + QStringLiteral(")\\s+(?:\"([^\"]+)\"\\s+as\\s+([\\w.]+)|([\\w.]+)\\s+as\\s+\"([^\"]+)\"|([\\w.]+)\\s+as\\s+"
                             "([\\w.]+)|\"([^\"]+)\"|([\\w.]+))(?:\\s+.*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

const QRegularExpression &plantumlKind()
{
    static const QRegularExpression pattern(QStringLiteral("^(") + kinds + QStringLiteral(")\\b"),
                                            QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// A name (possibly quoted), an arrow, a name, lifeline shortcuts, `: text`.
const QRegularExpression &plantumlMessage()
{
    static const QRegularExpression pattern(QStringLiteral(
        R"(^("[^"]+"|[\w.]+)\s*([<>xo\\/[\]?-]*-[<>xo\\/[\]?-]*)\s*("[^"]+"|[\w.]+)\s*([+*!-]{0,4})\s*(?::\s*(.*))?$)"));
    return pattern;
}

// Spacing and dividers: presentation only.
const QRegularExpression &plantumlPresentation()
{
    static const QRegularExpression pattern(QStringLiteral(R"(^(?:==|\.\.\.|\|\|))"));
    return pattern;
}

// Read but not kept: reported.
const QRegularExpression &plantumlDropped()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^(?:activate\b|deactivate\b|destroy\b|create\b|return\b|note\b|hnote\b|rnote\b|ref\b|box\b))"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

QString unquote(const QString &name)
{
    return plantumlText(name.startsWith(QLatin1Char('"')) ? name.mid(1, name.size() - 2) : name);
}

struct ArrowKind {
    QString line;
    QString head;
    QString startHead = QStringLiteral("none");
    bool reversed = false;
};

// A PlantUML arrow: a double dash is dotted (drawn dashed), the head
// characters decide the tip, a left head without a right one reverses it.
ArrowKind classifyArrow(const QString &op)
{
    static const QRegularExpression inlineStyle(QStringLiteral(R"(\[[^\]]*\])"));
    static const QRegularExpression rightHead(QStringLiteral(R"((>>|>|\\\\|\\|//|/|x|o)$)"));
    static const QRegularExpression leftHead(QStringLiteral("^(<<|<|x|o)"));
    QString clean = op;
    clean.remove(inlineStyle);
    ArrowKind kind;
    kind.line = clean.contains(QStringLiteral("--")) ? QStringLiteral("dashed") : QStringLiteral("solid");
    const QString right = rightHead.match(clean).captured(1);
    const QString left = leftHead.match(clean).captured(1);
    const auto headOf = [](const QString &token, const char *arrow) {
        return token == QLatin1String("x")     ? QStringLiteral("cross")
               : token == QLatin1String(arrow) ? QStringLiteral("arrow")
               : !token.isEmpty()              ? QStringLiteral("open")
                                               : QStringLiteral("none");
    };
    if (right.isEmpty() && !left.isEmpty()) {
        kind.head = headOf(left, "<");
        kind.reversed = true;
        return kind;
    }
    const QString head = headOf(right, ">");
    kind.head = head == QLatin1String("none") ? QStringLiteral("arrow") : head;
    kind.startHead = headOf(left, "<");
    return kind;
}

// A text in a PlantUML statement: \n is its line break.
QString plantumlEscape(const QString &text)
{
    QString result = text;
    return result.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
}

class PlantumlSequence : public Codec {
public:
    QString id() const override { return QStringLiteral("plantuml-sequence"); }
    QString language() const override { return QStringLiteral("plantuml"); }
    QString title() const override { return QStringLiteral("Sequence diagram"); }
    const Family &family() const override { return families::sequence(); }
    bool arrangeWithGraphviz() const override { return false; }

    bool matches(const QString &source) const override
    {
        static const QRegularExpression otherKinds(QStringLiteral("^(class|interface|enum|abstract|annotation|state)\\b"),
                                                   QRegularExpression::CaseInsensitiveOption);
        const QStringList lines = plantumlLines(source);
        for (const QString &line : lines) {
            if (otherKinds.match(line).hasMatch() || line.contains(QStringLiteral("[*]")))
                return false;
        }
        return std::any_of(lines.cbegin(), lines.cend(), [](const QString &line) {
            return plantumlMessage().match(line).hasMatch() || plantumlKind().match(line).hasMatch();
        });
    }

    bool parse(const QString &source, Model &model, QString *) const override
    {
        Nodes nodes;
        int order = 0;
        int fragmentSeq = 0;
        QList<Fragment> stack;
        const auto participant = [&](const QString &key, const QString &kind, const QString &alias = {}) {
            Node &node = nodes.get(key, kind);
            if (!alias.isEmpty())
                node.text = alias;
        };

        static const QRegularExpression open(QStringLiteral("^(loop|alt|opt|par|critical|break|group|rect)\\b\\s*(.*)$"),
                                             QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression branch(QStringLiteral("^(else|and)\\b\\s*(.*)$"),
                                               QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression end(QStringLiteral("^end\\b"), QRegularExpression::CaseInsensitiveOption);
        // `end note`, `end ref`, `end box` close those, not a group.
        static const QRegularExpression endOther(QStringLiteral(R"(^end\s*(?:note|hnote|rnote|ref|box)\b)"),
                                                 QRegularExpression::CaseInsensitiveOption);
        // A note or a ref without `:` spans lines up to its end.
        static const QRegularExpression blockStart(QStringLiteral(R"(^(note|hnote|rnote|ref)\b[^:]*$)"),
                                                   QRegularExpression::CaseInsensitiveOption);
        QString block;

        for (const QString &stmt : plantumlLines(source)) {
            if (!block.isEmpty()) {
                static const QRegularExpression blockEnd(QStringLiteral(R"(^end\s*(note|hnote|rnote|ref)\b)"),
                                                         QRegularExpression::CaseInsensitiveOption);
                if (blockEnd.match(stmt).hasMatch())
                    block.clear();
                continue;
            }
            if (const QRegularExpressionMatch m = plantumlDeclaration().match(stmt); m.hasMatch()) {
                const QString kind = m.captured(1).toLower();
                if (!m.captured(3).isEmpty())
                    participant(m.captured(3), kind, plantumlText(m.captured(2))); // "Long" as A
                else if (!m.captured(4).isEmpty())
                    participant(m.captured(4), kind, plantumlText(m.captured(5))); // A as "Long"
                else if (!m.captured(6).isEmpty())
                    participant(m.captured(7), kind, plantumlText(m.captured(6))); // Long as A
                else
                    participant(unquote(!m.captured(8).isEmpty() ? m.captured(8) : m.captured(9)), kind);
                continue;
            }
            if (const QRegularExpressionMatch m = open.match(stmt); m.hasMatch()) {
                Fragment f;
                f.id = QStringLiteral("fragment-%1").arg(++fragmentSeq);
                f.kind = m.captured(1).toLower();
                f.label = plantumlText(m.captured(2).trimmed());
                f.depth = int(stack.size());
                stack.append(f);
                continue;
            }
            if (const QRegularExpressionMatch m = branch.match(stmt); m.hasMatch() && !stack.isEmpty()) {
                stack.back().branchKeyword = m.captured(1).toLower();
                stack.back().branchLabel = plantumlText(m.captured(2).trimmed());
                continue;
            }
            if (endOther.match(stmt).hasMatch())
                continue;
            if (end.match(stmt).hasMatch()) {
                if (!stack.isEmpty())
                    stack.removeLast();
                continue;
            }
            if (plantumlPresentation().match(stmt).hasMatch())
                continue;
            if (plantumlDropped().match(stmt).hasMatch()) {
                if (const QRegularExpressionMatch m = blockStart.match(stmt); m.hasMatch())
                    block = m.captured(1);
                model.warnings.append(QStringLiteral("Not understood: %1").arg(stmt));
                continue;
            }
            const QRegularExpressionMatch msg = plantumlMessage().match(stmt);
            if (!msg.hasMatch()) {
                model.warnings.append(QStringLiteral("Not understood: %1").arg(stmt));
                continue;
            }
            const ArrowKind arrow = classifyArrow(msg.captured(2));
            const QString a = unquote(msg.captured(1)), b = unquote(msg.captured(3));
            participant(a, QStringLiteral("participant"));
            participant(b, QStringLiteral("participant"));
            const QString shortcut = msg.captured(4);
            model.links.append({QStringLiteral("message"), arrow.reversed ? b : a, arrow.reversed ? a : b,
                                QJsonObject{{QStringLiteral("label"), plantumlText(msg.captured(5).trimmed())},
                                            {QStringLiteral("line"), arrow.line},
                                            {QStringLiteral("head"), arrow.head},
                                            {QStringLiteral("startHead"), arrow.startHead},
                                            {QStringLiteral("activation"),
                                             shortcut.startsWith(QLatin1Char('+'))   ? QStringLiteral("+")
                                             : shortcut.startsWith(QLatin1Char('-')) ? QStringLiteral("-")
                                                                                     : QString()},
                                            {QStringLiteral("fragments"), fragmentsJson(stack)},
                                            {QStringLiteral("order"), order++}}});
        }
        nodes.toModel(model);
        model.meta = QJsonObject{};
        return true;
    }

    QString serialize(const Board &board, const QJsonObject &) const override
    {
        static const QRegularExpression knownKind(QStringLiteral("^(") + kinds + QStringLiteral(")$"),
                                                  QRegularExpression::CaseInsensitiveOption);
        QSet<QString> used;
        QHash<QString, QString> keys;
        QStringList out{QStringLiteral("@startuml")};
        for (const ParticipantShape *s : participantsOf(board)) {
            const QString key = keyFor(s->key, s->text, used);
            keys.insert(s->id, plantumlName(key));
            const QString kind = knownKind.match(s->kind).hasMatch() ? s->kind : QStringLiteral("participant");
            out.append(!s->text.isEmpty() && s->text != key
                           ? QStringLiteral("%1 \"%2\" as %3").arg(kind, plantumlEscape(s->text), plantumlName(key))
                           : QStringLiteral("%1 %2").arg(kind, plantumlName(key)));
        }
        QList<Fragment> active;
        for (const MessageLink *l : messagesOf(board)) {
            const QString from = keys.value(l->from), to = keys.value(l->to);
            if (from.isEmpty() || to.isEmpty())
                continue;
            active = syncFragments(out, active, l->fragments, {}, false, plantumlEscape);
            const QString dash = l->line == QLatin1String("dashed") ? QStringLiteral("--") : QStringLiteral("-");
            const QString head = l->head == QLatin1String("cross")  ? QStringLiteral(">x")
                                 : l->head == QLatin1String("open") ? QStringLiteral(">>")
                                                                     : QStringLiteral(">");
            const QString start = l->startHead == QLatin1String("arrow")   ? QStringLiteral("<")
                                  : l->startHead == QLatin1String("cross") ? QStringLiteral("x")
                                                                           : QString();
            // PlantUML's lifeline shortcuts are doubled: `++` activates the
            // receiver, `--` deactivates the sender.
            const QString shortcut = l->activation == QLatin1String("+")   ? QStringLiteral(" ++")
                                     : l->activation == QLatin1String("-") ? QStringLiteral(" --")
                                                                           : QString();
            out.append(QStringLiteral("%1 %2%3%4 %5%6").arg(from, start, dash, head, to, shortcut)
                       + (l->label.isEmpty() ? QString() : QStringLiteral(" : ") + plantumlEscape(l->label)));
        }
        for (qsizetype i = active.size() - 1; i >= 0; --i)
            out.append(QStringLiteral("end"));
        out.append(QStringLiteral("@enduml"));
        return out.join(QLatin1Char('\n'));
    }

    QString blank() const override
    {
        return QStringLiteral("@startuml\nparticipant \"Actor\" as A\nparticipant \"System\" as S\n"
                              "A -> S : First interaction\n@enduml");
    }
};

} // namespace

} // namespace diagram::sequence

namespace diagram::families {

QList<const Codec *> sequenceCodecs()
{
    static const sequence::MermaidSequence mermaid;
    static const sequence::PlantumlSequence plantuml;
    return {&mermaid, &plantuml};
}

} // namespace diagram::families
