// SPDX-License-Identifier: GPL-3.0-only

#include "Document.h"

#include "Board.h"
#include "Family.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <optional>

namespace diagram::document {

namespace {

const QRegularExpression &layoutLine()
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*(?:%%|')\\s*duet\\s+(\\{.*\\})\\s*$"));
    return pattern;
}

const QRegularExpression &layoutHeader()
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*(?:%%|')\\s*Layout kept by Duet\\b"));
    return pattern;
}

QString commentPrefix(const Codec &codec)
{
    return codec.language() == QLatin1String("plantuml") ? QStringLiteral("'") : QStringLiteral("%%");
}

qreal rounded(qreal value)
{
    return std::round(value * 10) / 10;
}

QJsonArray flat(const std::vector<QPointF> &points)
{
    QJsonArray array;
    for (const QPointF &p : points) {
        array.append(rounded(p.x()));
        array.append(rounded(p.y()));
    }
    return array;
}

std::vector<QPointF> unflat(const QJsonArray &array)
{
    std::vector<QPointF> points;
    for (qsizetype i = 0; i + 1 < array.size(); i += 2)
        points.emplace_back(array[i].toDouble(), array[i + 1].toDouble());
    return points;
}

QJsonArray pair(QPointF p)
{
    return QJsonArray{rounded(p.x()), rounded(p.y())};
}

std::optional<QPointF> unpair(const QJsonValue &value)
{
    const QJsonArray array = value.toArray();
    if (array.size() != 2)
        return std::nullopt;
    return QPointF(array[0].toDouble(), array[1].toDouble());
}

// The saved layout of a source: entries by shape key, and by link (ends'
// keys, rank among the links joining them).
struct SavedLayout {
    QHash<QString, QJsonObject> shapes;
    QHash<QString, QList<QJsonObject>> links;
    bool any = false;
};

// Splits a file into its source and its layout comments.
QString splitLayout(const QString &content, SavedLayout &layout)
{
    QStringList kept;
    static const QRegularExpression newline(QStringLiteral("\\r?\\n"));
    for (const QString &line : content.split(newline)) {
        const QRegularExpressionMatch m = layoutLine().match(line);
        if (m.hasMatch()) {
            const QJsonObject entry = QJsonDocument::fromJson(m.captured(1).toUtf8()).object();
            if (entry.contains(QStringLiteral("key"))) {
                layout.shapes.insert(entry.value(QStringLiteral("key")).toString(), entry);
                layout.any = true;
            } else if (entry.contains(QStringLiteral("link"))) {
                layout.links[entry.value(QStringLiteral("link")).toString()].append(entry);
                layout.any = true;
            }
            continue;
        }
        if (layoutHeader().match(line).hasMatch())
            continue;
        kept.append(line);
    }
    while (!kept.isEmpty() && kept.back().trimmed().isEmpty())
        kept.removeLast();
    return kept.join(QLatin1Char('\n'));
}

QString linkKey(const Board &board, const Link &l)
{
    const Shape *a = board.shape(l.from);
    const Shape *b = board.shape(l.to);
    return (a ? a->key : QString()) + QLatin1Char('>') + (b ? b->key : QString());
}

void restoreShape(const Board &board, Shape &s, const QJsonObject &saved)
{
    s.x = saved.value(QStringLiteral("x")).toDouble();
    s.y = saved.value(QStringLiteral("y")).toDouble();
    if (saved.contains(QStringLiteral("w")))
        s.manualWidth = saved.value(QStringLiteral("w")).toDouble();
    if (saved.contains(QStringLiteral("h")))
        s.manualHeight = saved.value(QStringLiteral("h")).toDouble();
    const QString fill = saved.value(QStringLiteral("fill")).toString();
    if (isHexColor(fill))
        s.customFill = fill.toLower();
    board.measureShape(s);
}

bool isLineStyle(const QString &line)
{
    return line == QLatin1String("solid") || line == QLatin1String("dashed") || line == QLatin1String("dotted");
}

bool isCubicCurve(const std::vector<QPointF> &curve)
{
    return curve.size() >= 4 && (curve.size() - 1) % 3 == 0;
}

// The saved curve, or else the saved label point, both against the ends'
// current places.
void restoreRoute(const Shape &a, const Shape &b, Link &l, const QJsonObject &saved)
{
    const std::vector<QPointF> curve = unflat(saved.value(QStringLiteral("c")).toArray());
    if (isCubicCurve(curve)) {
        Spline spline;
        spline.points = curve;
        spline.start = unpair(saved.value(QStringLiteral("s")));
        spline.end = unpair(saved.value(QStringLiteral("e")));
        spline.labelPoint = unpair(saved.value(QStringLiteral("lp")));
        spline.fromAnchor = QPointF(a.x, a.y);
        spline.toAnchor = QPointF(b.x, b.y);
        l.spline = spline;
        return;
    }
    if (const std::optional<QPointF> lp = unpair(saved.value(QStringLiteral("lp"))))
        l.layoutLabelPoint = Link::ReservedLabel{*lp, QPointF(a.x, a.y), QPointF(b.x, b.y)};
}

void restoreLink(const Board &board, Link &l, const QJsonObject &saved)
{
    const QString line = saved.value(QStringLiteral("style")).toString();
    if (isLineStyle(line))
        l.customLineStyle = line;
    const QString color = saved.value(QStringLiteral("color")).toString();
    if (isHexColor(color))
        l.customColor = color.toLower();
    const qreal width = saved.value(QStringLiteral("width")).toDouble();
    if (width >= linkWidthMin && width <= linkWidthMax)
        l.customWidth = width;
    const Shape *a = board.shape(l.from);
    const Shape *b = board.shape(l.to);
    if (a && b)
        restoreRoute(*a, *b, l, saved);
}

// Gives the built board's elements their saved place, size, color and
// curve. Returns the shapes left without a place.
QStringList restore(const Board &board, const SavedLayout &layout)
{
    QStringList unplaced;
    for (const auto &s : board.shapes()) {
        const QJsonObject saved = layout.shapes.value(s->key);
        const bool hasPlace = !s->key.isEmpty() && !saved.isEmpty() && saved.contains(QStringLiteral("x"));
        if (hasPlace)
            restoreShape(board, *s, saved);
        else
            unplaced.append(s->id);
    }
    // Several links can join the same shapes (sequence messages): they get
    // their saved appearance back in order.
    QHash<QString, int> taken;
    for (const auto &l : board.links()) {
        const QString key = linkKey(board, *l);
        const QList<QJsonObject> candidates = layout.links.value(key);
        const int rank = taken.value(key);
        if (rank >= candidates.size())
            continue;
        taken.insert(key, rank + 1);
        restoreLink(board, *l, candidates[rank]);
    }
    return unplaced;
}

bool differsFromMinimum(const std::optional<qreal> &manual, qreal minimum)
{
    return manual && std::abs(*manual - minimum) > 0.5;
}

QJsonObject shapeLayoutEntry(const Shape &s)
{
    QJsonObject entry{
        {QStringLiteral("key"), s.key}, {QStringLiteral("x"), rounded(s.x)}, {QStringLiteral("y"), rounded(s.y)}};
    // A frame laid out as a cluster takes its size from the layout, not
    // from its content: measuring could not give it back.
    const bool laidOutSize = s.isGraphvizCluster();
    if (laidOutSize || differsFromMinimum(s.manualWidth, s.resizeMinWidth))
        entry.insert(QStringLiteral("w"), rounded(s.w));
    if (laidOutSize || differsFromMinimum(s.manualHeight, s.resizeMinHeight))
        entry.insert(QStringLiteral("h"), rounded(s.h));
    if (!s.customFill.isEmpty())
        entry.insert(QStringLiteral("fill"), s.customFill);
    return entry;
}

// The link's curve, or else its label point, while they still fit its ends.
void insertRoute(const Board &board, const Link &l, QJsonObject &entry)
{
    const Shape *a = board.shape(l.from);
    const Shape *b = board.shape(l.to);
    if (!a || !b)
        return;
    if (const Spline *spline = l.validSpline(*a, *b)) {
        entry.insert(QStringLiteral("c"), flat(spline->points));
        if (spline->start)
            entry.insert(QStringLiteral("s"), pair(*spline->start));
        if (spline->end)
            entry.insert(QStringLiteral("e"), pair(*spline->end));
        if (spline->labelPoint)
            entry.insert(QStringLiteral("lp"), pair(*spline->labelPoint));
    } else if (const std::optional<QPointF> lp = l.validLayoutLabelPoint(*a, *b)) {
        entry.insert(QStringLiteral("lp"), pair(*lp));
    }
}

QJsonObject linkLayoutEntry(const Board &board, const Link &l, const QString &key, int rank)
{
    QJsonObject entry{{QStringLiteral("link"), key}, {QStringLiteral("n"), rank}};
    if (!l.customLineStyle.isEmpty())
        entry.insert(QStringLiteral("style"), l.customLineStyle);
    if (!l.customColor.isEmpty())
        entry.insert(QStringLiteral("color"), l.customColor);
    if (l.customWidth)
        entry.insert(QStringLiteral("width"), *l.customWidth);
    insertRoute(board, l, entry);
    return entry;
}

QStringList layoutLines(const Board &board, const Codec &codec)
{
    const QString prefix = commentPrefix(codec) + QStringLiteral(" duet ");
    const auto line = [&prefix](const QJsonObject &entry) {
        return prefix + QString::fromUtf8(QJsonDocument(entry).toJson(QJsonDocument::Compact));
    };
    QStringList lines;
    for (const auto &s : board.shapes()) {
        if (!s->key.isEmpty())
            lines.append(line(shapeLayoutEntry(*s)));
    }
    QHash<QString, int> ranks;
    for (const auto &l : board.links()) {
        const QString key = linkKey(board, *l);
        const int rank = ranks.value(key);
        ranks.insert(key, rank + 1);
        const QJsonObject entry = linkLayoutEntry(board, *l, key, rank);
        // Beyond "link" and "n", there is something to keep.
        if (entry.size() > 2)
            lines.append(line(entry));
    }
    if (!lines.isEmpty())
        lines.prepend(commentPrefix(codec) + QStringLiteral(" Layout kept by Duet (positions, sizes, colors, curves)"));
    return lines;
}

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

// The keys of the nodes whose source gives no place.
QSet<QString> unplacedKeys(const Model &model)
{
    QSet<QString> keys;
    for (const Model::Node &node : model.nodes) {
        if (!node.props.contains(QStringLiteral("x")) || !node.props.contains(QStringLiteral("y")))
            keys.insert(node.key);
    }
    return keys;
}

// The shapes left without a place: by the source when it owns the layout,
// else by the saved layout.
QStringList unplacedShapes(const Board &board, const Codec &codec, const Model &model, const SavedLayout &layout)
{
    if (!codec.sourceOwnsLayout())
        return restore(board, layout);
    const QSet<QString> keys = unplacedKeys(model);
    QStringList unplaced;
    for (const auto &s : board.shapes()) {
        if (keys.contains(s->key))
            unplaced.append(s->id);
    }
    return unplaced;
}

bool hasUnroutedLinks(const Board &board)
{
    return std::ranges::any_of(board.links(), [](const auto &l) { return !l->spline && l->from != l->to; });
}

enum class History {
    Clear,
    Keep
};

struct BuildRequest {
    const Codec *codec = nullptr;
    QString source;
    const SavedLayout *layout = nullptr;
    History history = History::Clear;
};

void decideLayout(const Board &board, const Codec &codec, const QStringList &unplaced, Loaded &loaded)
{
    loaded.needsArrange = false;
    loaded.unplaced.clear();
    loaded.needsRouting = false;
    const Family &family = codec.family();
    if (!board.isEmpty()) {
        const bool nothingPlaced = unplaced.size() == qsizetype(board.shapes().size());
        const bool familyArranges = family.alwaysArrange && codec.arrangeWithGraphviz() && !codec.sourceOwnsLayout();
        if (nothingPlaced || familyArranges)
            loaded.needsArrange = true;
        else
            loaded.unplaced = unplaced;
    }
    if (!loaded.needsArrange && family.routeLinksOnMove)
        loaded.needsRouting = hasUnroutedLinks(board);
}

std::optional<Loaded> build(Board &board, const BuildRequest &request, QString *error)
{
    const Codec &codec = *request.codec;
    Model model;
    QString message;
    if (!codec.parse(request.source, model, &message)) {
        setError(error, message.isEmpty() ? QStringLiteral("The diagram cannot be read") : message);
        return std::nullopt;
    }
    buildBoard(board, codec, model);
    Loaded loaded;
    loaded.codec = &codec;
    loaded.warnings = model.warnings;
    decideLayout(board, codec, unplacedShapes(board, codec, model, *request.layout), loaded);
    if (request.history == History::Clear)
        board.clearHistory();
    return loaded;
}

// The codec of a blank file: the first family in menu order written in
// that language (the flowchart for Mermaid).
const Codec *blankFileCodec(const QString &language)
{
    for (const Family *family : allFamilies()) {
        for (const Codec *candidate : allCodecs()) {
            if (&candidate->family() == family && candidate->language() == language)
                return candidate;
        }
    }
    return nullptr;
}

QString unsupportedMessage(const QString &language)
{
    if (language == QLatin1String("mermaid"))
        return QStringLiteral("Unsupported Mermaid diagram (the first line names its kind: flowchart, "
                              "sequenceDiagram, classDiagram…)");
    return QStringLiteral("Unsupported diagram");
}

} // namespace

std::optional<Loaded> load(Board &board, const QString &path, const QByteArray &content, QString *error)
{
    const QString language = languageOfFile(path);
    if (language.isEmpty()) {
        setError(error, QStringLiteral("Not a diagram file"));
        return std::nullopt;
    }
    SavedLayout layout;
    QString source = splitLayout(QString::fromUtf8(content), layout);
    const Codec *codec = codecFor(language, source);
    // A blank file is a new diagram of the first family written in that
    // language.
    if (!codec && source.trimmed().isEmpty()) {
        codec = blankFileCodec(language);
        if (codec)
            source = codec->blank();
    }
    if (!codec) {
        setError(error, unsupportedMessage(language));
        return std::nullopt;
    }
    return build(board, {.codec = codec, .source = source, .layout = &layout}, error);
}

QString source(const Board &board, const Codec &codec)
{
    return codec.serialize(board, board.meta);
}

QByteArray save(const Board &board, const Codec &codec)
{
    QString text = codec.serialize(board, board.meta);
    if (!codec.sourceOwnsLayout()) {
        const QStringList lines = layoutLines(board, codec);
        if (!lines.isEmpty()) {
            // PlantUML ignores what follows @enduml: the layout goes before it.
            static const QRegularExpression end(QStringLiteral("\\n\\s*@enduml\\s*$"));
            const QRegularExpressionMatch m = end.match(text);
            if (codec.language() == QLatin1String("plantuml") && m.hasMatch())
                text = text.left(m.capturedStart()) + QStringLiteral("\n\n") + lines.join(QLatin1Char('\n')) +
                       QStringLiteral("\n@enduml");
            else
                text += QStringLiteral("\n\n") + lines.join(QLatin1Char('\n'));
        }
    }
    if (!text.endsWith(QLatin1Char('\n')))
        text += QLatin1Char('\n');
    return text.toUtf8();
}

std::optional<Loaded> applySource(Board &board, const Codec &codec, const QString &text, QString *error)
{
    // The current arrangement is the layout to keep.
    SavedLayout layout;
    splitLayout(QString::fromUtf8(save(board, codec)), layout);
    SavedLayout ignored;
    const QString cleaned = splitLayout(text, ignored);
    const Codec *target = codecFor(codec.language(), cleaned);
    if (!target) {
        setError(error, QStringLiteral("This is not a diagram Duet reads"));
        return std::nullopt;
    }
    Board scratch;
    if (!build(scratch, {.codec = target, .source = cleaned, .layout = &layout}, error))
        return std::nullopt;
    const QByteArray before = board.snapshot();
    // The scratch build above read the same source: this one succeeds.
    const std::optional<Loaded> applied =
        build(board, {.codec = target, .source = cleaned, .layout = &layout, .history = History::Keep}, error);
    board.pushHistory(before);
    return applied.value_or(Loaded{});
}

QByteArray blank(const QString &familyId)
{
    const Family *family = findFamily(familyId);
    const Codec *codec = family ? defaultCodec(*family) : nullptr;
    if (!codec)
        return {};
    QString text = codec->blank();
    if (!text.endsWith(QLatin1Char('\n')))
        text += QLatin1Char('\n');
    return text.toUtf8();
}

QString extension(const QString &familyId)
{
    const Family *family = findFamily(familyId);
    const Codec *codec = family ? defaultCodec(*family) : nullptr;
    if (!codec)
        return {};
    if (codec->language() == QLatin1String("board"))
        return QStringLiteral("board");
    if (codec->language() == QLatin1String("plantuml"))
        return QStringLiteral("puml");
    if (codec->language() == QLatin1String("flowboard"))
        return QStringLiteral("flow");
    return QStringLiteral("mmd");
}

QStringList problems(const QString &path, const QByteArray &content)
{
    Board board;
    QString error;
    const std::optional<Loaded> loaded = load(board, path, content, &error);
    if (!loaded)
        return {error};
    return loaded->warnings;
}

} // namespace diagram::document
