// SPDX-License-Identifier: GPL-3.0-only

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
#include <array>

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

// The fragments a source has opened and not closed yet, innermost last.
class FragmentStack {
public:
    bool isEmpty() const { return stack_.isEmpty(); }

    void open(const QString &kind, const QString &label)
    {
        Fragment f;
        f.id = QStringLiteral("fragment-%1").arg(++opened_);
        f.kind = kind;
        f.label = label;
        f.depth = int(stack_.size());
        stack_.append(f);
    }

    // Precondition: a fragment is open.
    void switchBranch(const QString &keyword, const QString &label)
    {
        stack_.back().branchKeyword = keyword;
        stack_.back().branchLabel = label;
    }

    // An `end` with nothing open is ignored.
    void close()
    {
        if (!stack_.isEmpty())
            stack_.removeLast();
    }

    QJsonArray toJson() const { return fragmentsJson(stack_); }

private:
    QList<Fragment> stack_;
    int opened_ = 0;
};

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
const auto arrows = std::to_array<Arrow>({
    {"<<-->>", "dashed", "arrow", "arrow"},
    {"<<->>", "solid", "arrow", "arrow"},
    {"-->>", "dashed", "arrow", "none"},
    {"->>", "solid", "arrow", "none"},
    {"--x", "dashed", "cross", "none"},
    {"-x", "solid", "cross", "none"},
    {"--)", "dashed", "open", "none"},
    {"-)", "solid", "open", "none"},
    {"-->", "dashed", "none", "none"},
    {"->", "solid", "none", "none"},
});

const QRegularExpression &mermaidMessage()
{
    static const QRegularExpression pattern = [] {
        QStringList alternatives;
        for (const Arrow &arrow : arrows)
            alternatives.append(QRegularExpression::escape(QLatin1String(arrow.token)));
        return QRegularExpression(QStringLiteral("^(.+?)\\s*(") + alternatives.join(QLatin1Char('|')) +
                                  QStringLiteral(")\\s*([+-]?)\\s*([^:]+?)\\s*(?::\\s*(.*))?$"));
    }();
    return pattern;
}

const QRegularExpression &mermaidHeader()
{
    static const QRegularExpression pattern(QStringLiteral("^sequenceDiagram\\b"),
                                            QRegularExpression::CaseInsensitiveOption);
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
    static const QRegularExpression pattern(QStringLiteral("^(?:activate\\b|deactivate\\b|destroy\\b|box\\b|note\\b)"),
                                            QRegularExpression::CaseInsensitiveOption);
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

// `;` separates Mermaid statements, but it also ends the entities (&amp;
// #quot;): they are hidden before splitting.
QStringList mermaidStatements(const QString &source)
{
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
    return statements;
}

// Reads a Mermaid sequence into a model, one statement at a time.
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
        if (readDeclaration(stmt) || readFragment(stmt))
            return;
        if (mermaidPresentation().match(stmt).hasMatch())
            return;
        readMessage(stmt);
    }

    void finish()
    {
        nodes_.toModel(model_);
        model_.meta = QJsonObject{};
    }

private:
    bool readDeclaration(const QString &stmt)
    {
        static const QRegularExpression declaration(
            QStringLiteral("^(?:create\\s+)?(participant|actor)\\s+(.+?)(?:\\s+as\\s+(.+))?$"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = declaration.match(stmt);
        if (!m.hasMatch())
            return false;
        const QString key = decodeLabel(m.captured(2)).trimmed();
        const QString kind = m.captured(1).toLower();
        Node &node = nodes_.get(key, kind);
        if (m.hasCaptured(3) && !m.captured(3).isEmpty())
            node.text = decodeLabel(m.captured(3).trimmed());
        if (kind == QLatin1String("actor"))
            node.kind = kind;
        return true;
    }

    // A fragment opened, a branch of the innermost one, or an end.
    bool readFragment(const QString &stmt)
    {
        static const QRegularExpression open(QStringLiteral("^(loop|alt|opt|par|critical|break|rect)\\b\\s*(.*)$"),
                                             QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression branch(QStringLiteral("^(else|and|option)\\b\\s*(.*)$"),
                                               QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression end(QStringLiteral("^end\\b"), QRegularExpression::CaseInsensitiveOption);
        if (const QRegularExpressionMatch m = open.match(stmt); m.hasMatch()) {
            fragments_.open(m.captured(1).toLower(), decodeLabel(m.captured(2).trimmed()));
            return true;
        }
        if (const QRegularExpressionMatch m = branch.match(stmt); m.hasMatch() && !fragments_.isEmpty()) {
            fragments_.switchBranch(m.captured(1).toLower(), decodeLabel(m.captured(2).trimmed()));
            return true;
        }
        if (!end.match(stmt).hasMatch())
            return false;
        fragments_.close();
        return true;
    }

    void readMessage(const QString &stmt)
    {
        static const QRegularExpression wrap(QStringLiteral("^(?:wrap|nowrap):\\s*"),
                                             QRegularExpression::CaseInsensitiveOption);
        // A dropped statement is reported even when it reads as a message.
        const bool isDropped = mermaidDropped().match(stmt).hasMatch();
        const QRegularExpressionMatch msg = isDropped ? QRegularExpressionMatch() : mermaidMessage().match(stmt);
        if (!msg.hasMatch()) {
            model_.warnings.append(QStringLiteral("Not understood: %1").arg(stmt));
            return;
        }
        const QString token = msg.captured(2);
        // The message pattern only matches the tokens of `arrows`.
        const Arrow &arrow =
            *std::ranges::find_if(arrows, [&](const Arrow &a) { return token == QLatin1String(a.token); });
        const QString from = nodes_.get(msg.captured(1).trimmed(), QStringLiteral("participant")).key;
        const QString to = nodes_.get(msg.captured(4).trimmed(), QStringLiteral("participant")).key;
        QString text = msg.captured(5);
        text.remove(wrap);
        model_.links.append({QStringLiteral("message"), from, to,
                             QJsonObject{{QStringLiteral("label"), decodeLabel(text.trimmed())},
                                         {QStringLiteral("line"), QLatin1String(arrow.line)},
                                         {QStringLiteral("head"), QLatin1String(arrow.head)},
                                         {QStringLiteral("startHead"), QLatin1String(arrow.startHead)},
                                         {QStringLiteral("activation"), msg.captured(3)},
                                         {QStringLiteral("fragments"), fragments_.toJson()},
                                         {QStringLiteral("order"), order_++}}});
    }

    Model &model_;
    Nodes nodes_;
    FragmentStack fragments_;
    int order_ = 0;
};

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
        const QStringList lines = mermaidLines(source);
        return !lines.isEmpty() && mermaidHeader().match(lines.first()).hasMatch();
    }

    bool parse(const QString &source, Model &model, QString *) const override
    {
        MermaidReader reader(model);
        for (const QString &stmt : mermaidStatements(source))
            reader.read(stmt);
        reader.finish();
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
            const QString from = keys.value(l->from);
            const QString to = keys.value(l->to);
            if (from.isEmpty() || to.isEmpty())
                continue;
            active = syncFragments(out, active, l->fragments, {QStringLiteral("  "), true, mermaidText});
            out.append(QStringLiteral("  ") + from + mermaidArrow(*l) + l->activation + to + QStringLiteral(": ") +
                       mermaidText(l->label));
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
        QStringLiteral("^(") + kinds +
            QStringLiteral(")\\s+(?:\"([^\"]+)\"\\s+as\\s+([\\w.]+)|([\\w.]+)\\s+as\\s+\"([^\"]+)\"|([\\w.]+)\\s+as\\s+"
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
        QStringLiteral(
            R"(^(?:activate\b|deactivate\b|destroy\b|create\b|return\b|note\b|hnote\b|rnote\b|ref\b|box\b))"),
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

// The tip a PlantUML head token draws: `x` a cross, the plain `arrow`
// character an arrow, any other one an open head.
QString headOf(const QString &token, const char *arrow)
{
    if (token == QLatin1String("x"))
        return QStringLiteral("cross");
    if (token == QLatin1String(arrow))
        return QStringLiteral("arrow");
    if (!token.isEmpty())
        return QStringLiteral("open");
    return QStringLiteral("none");
}

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

// The activation a PlantUML lifeline shortcut (`++`, `--`…) gives a message.
QString activationOf(const QString &shortcut)
{
    if (shortcut.startsWith(QLatin1Char('+')))
        return QStringLiteral("+");
    if (shortcut.startsWith(QLatin1Char('-')))
        return QStringLiteral("-");
    return {};
}

// The arrow written for a message: start decoration, dashes, head.
QString plantumlArrow(const MessageLink &l)
{
    QString start;
    if (l.startHead == QLatin1String("arrow"))
        start = QStringLiteral("<");
    else if (l.startHead == QLatin1String("cross"))
        start = QStringLiteral("x");
    const QString dash = l.line == QLatin1String("dashed") ? QStringLiteral("--") : QStringLiteral("-");
    QString head = QStringLiteral(">");
    if (l.head == QLatin1String("cross"))
        head = QStringLiteral(">x");
    else if (l.head == QLatin1String("open"))
        head = QStringLiteral(">>");
    return start + dash + head;
}

// PlantUML's lifeline shortcuts are doubled: `++` activates the receiver,
// `--` deactivates the sender.
QString plantumlShortcut(const QString &activation)
{
    if (activation == QLatin1String("+"))
        return QStringLiteral(" ++");
    if (activation == QLatin1String("-"))
        return QStringLiteral(" --");
    return {};
}

// Reads a PlantUML sequence into a model, one statement at a time.
class PlantumlReader {
public:
    explicit PlantumlReader(Model &model)
        : model_(model)
    {
    }

    void read(const QString &stmt)
    {
        if (skipBlock(stmt))
            return;
        if (readDeclaration(stmt) || readFragment(stmt))
            return;
        if (plantumlPresentation().match(stmt).hasMatch())
            return;
        if (readDropped(stmt))
            return;
        readMessage(stmt);
    }

    void finish()
    {
        nodes_.toModel(model_);
        model_.meta = QJsonObject{};
    }

private:
    // Inside a multi-line note or ref, everything up to its end is skipped.
    bool skipBlock(const QString &stmt)
    {
        if (block_.isEmpty())
            return false;
        static const QRegularExpression blockEnd(QStringLiteral(R"(^end\s*(note|hnote|rnote|ref)\b)"),
                                                 QRegularExpression::CaseInsensitiveOption);
        if (blockEnd.match(stmt).hasMatch())
            block_.clear();
        return true;
    }

    void declare(const QString &key, const QString &kind, const QString &alias = {})
    {
        Node &node = nodes_.get(key, kind);
        if (!alias.isEmpty())
            node.text = alias;
    }

    bool readDeclaration(const QString &stmt)
    {
        const QRegularExpressionMatch m = plantumlDeclaration().match(stmt);
        if (!m.hasMatch())
            return false;
        const QString kind = m.captured(1).toLower();
        if (!m.captured(3).isEmpty())
            declare(m.captured(3), kind, plantumlText(m.captured(2))); // "Long" as A
        else if (!m.captured(4).isEmpty())
            declare(m.captured(4), kind, plantumlText(m.captured(5))); // A as "Long"
        else if (!m.captured(6).isEmpty())
            declare(m.captured(7), kind, plantumlText(m.captured(6))); // Long as A
        else
            declare(unquote(!m.captured(8).isEmpty() ? m.captured(8) : m.captured(9)), kind);
        return true;
    }

    // A group opened, a branch of the innermost one, or an end.
    bool readFragment(const QString &stmt)
    {
        static const QRegularExpression open(
            QStringLiteral("^(loop|alt|opt|par|critical|break|group|rect)\\b\\s*(.*)$"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression branch(QStringLiteral("^(else|and)\\b\\s*(.*)$"),
                                               QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression end(QStringLiteral("^end\\b"), QRegularExpression::CaseInsensitiveOption);
        // `end note`, `end ref`, `end box` close those, not a group.
        static const QRegularExpression endOther(QStringLiteral(R"(^end\s*(?:note|hnote|rnote|ref|box)\b)"),
                                                 QRegularExpression::CaseInsensitiveOption);
        if (const QRegularExpressionMatch m = open.match(stmt); m.hasMatch()) {
            fragments_.open(m.captured(1).toLower(), plantumlText(m.captured(2).trimmed()));
            return true;
        }
        if (const QRegularExpressionMatch m = branch.match(stmt); m.hasMatch() && !fragments_.isEmpty()) {
            fragments_.switchBranch(m.captured(1).toLower(), plantumlText(m.captured(2).trimmed()));
            return true;
        }
        if (endOther.match(stmt).hasMatch())
            return true;
        if (!end.match(stmt).hasMatch())
            return false;
        fragments_.close();
        return true;
    }

    // Read but not kept: reported. A note or a ref without `:` spans lines
    // up to its end.
    bool readDropped(const QString &stmt)
    {
        static const QRegularExpression blockStart(QStringLiteral(R"(^(note|hnote|rnote|ref)\b[^:]*$)"),
                                                   QRegularExpression::CaseInsensitiveOption);
        if (!plantumlDropped().match(stmt).hasMatch())
            return false;
        if (const QRegularExpressionMatch m = blockStart.match(stmt); m.hasMatch())
            block_ = m.captured(1);
        model_.warnings.append(QStringLiteral("Not understood: %1").arg(stmt));
        return true;
    }

    void readMessage(const QString &stmt)
    {
        const QRegularExpressionMatch msg = plantumlMessage().match(stmt);
        if (!msg.hasMatch()) {
            model_.warnings.append(QStringLiteral("Not understood: %1").arg(stmt));
            return;
        }
        const ArrowKind arrow = classifyArrow(msg.captured(2));
        const QString a = unquote(msg.captured(1));
        const QString b = unquote(msg.captured(3));
        declare(a, QStringLiteral("participant"));
        declare(b, QStringLiteral("participant"));
        model_.links.append({QStringLiteral("message"), arrow.reversed ? b : a, arrow.reversed ? a : b,
                             QJsonObject{{QStringLiteral("label"), plantumlText(msg.captured(5).trimmed())},
                                         {QStringLiteral("line"), arrow.line},
                                         {QStringLiteral("head"), arrow.head},
                                         {QStringLiteral("startHead"), arrow.startHead},
                                         {QStringLiteral("activation"), activationOf(msg.captured(4))},
                                         {QStringLiteral("fragments"), fragments_.toJson()},
                                         {QStringLiteral("order"), order_++}}});
    }

    Model &model_;
    Nodes nodes_;
    FragmentStack fragments_;
    int order_ = 0;
    // The kind of the multi-line note or ref being skipped, or empty.
    QString block_;
};

class PlantumlSequence : public Codec {
public:
    QString id() const override { return QStringLiteral("plantuml-sequence"); }

    QString language() const override { return QStringLiteral("plantuml"); }

    QString title() const override { return QStringLiteral("Sequence diagram"); }

    const Family &family() const override { return families::sequence(); }

    bool arrangeWithGraphviz() const override { return false; }

    bool matches(const QString &source) const override
    {
        static const QRegularExpression otherKinds(
            QStringLiteral("^(class|interface|enum|abstract|annotation|state)\\b"),
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
        PlantumlReader reader(model);
        for (const QString &stmt : plantumlLines(source))
            reader.read(stmt);
        reader.finish();
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
            const QString from = keys.value(l->from);
            const QString to = keys.value(l->to);
            if (from.isEmpty() || to.isEmpty())
                continue;
            active = syncFragments(out, active, l->fragments, {.encode = plantumlEscape});
            out.append(QStringLiteral("%1 %2 %3%4").arg(from, plantumlArrow(*l), to, plantumlShortcut(l->activation)) +
                       (l->label.isEmpty() ? QString() : QStringLiteral(" : ") + plantumlEscape(l->label)));
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
