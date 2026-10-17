#include "Document.h"

#include "Board.h"
#include "Family.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <cmath>

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

// Gives the built board's elements their saved place, size, color and
// curve. Returns the shapes left without a place.
QStringList restore(Board &board, const SavedLayout &layout)
{
    QStringList unplaced;
    for (const auto &s : board.shapes()) {
        const QJsonObject saved = layout.shapes.value(s->key);
        if (s->key.isEmpty() || saved.isEmpty() || !saved.contains(QStringLiteral("x"))) {
            unplaced.append(s->id);
            continue;
        }
        s->x = saved.value(QStringLiteral("x")).toDouble();
        s->y = saved.value(QStringLiteral("y")).toDouble();
        if (saved.contains(QStringLiteral("w")))
            s->manualWidth = saved.value(QStringLiteral("w")).toDouble();
        if (saved.contains(QStringLiteral("h")))
            s->manualHeight = saved.value(QStringLiteral("h")).toDouble();
        const QString fill = saved.value(QStringLiteral("fill")).toString();
        if (isHexColor(fill))
            s->customFill = fill.toLower();
        board.measureShape(*s);
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
        const QJsonObject saved = candidates[rank];
        const QString line = saved.value(QStringLiteral("style")).toString();
        if (line == QLatin1String("solid") || line == QLatin1String("dashed") || line == QLatin1String("dotted"))
            l->customLineStyle = line;
        const QString color = saved.value(QStringLiteral("color")).toString();
        if (isHexColor(color))
            l->customColor = color.toLower();
        const qreal width = saved.value(QStringLiteral("width")).toDouble();
        if (width >= LinkWidthMin && width <= LinkWidthMax)
            l->customWidth = width;
        const Shape *a = board.shape(l->from);
        const Shape *b = board.shape(l->to);
        const std::vector<QPointF> curve = unflat(saved.value(QStringLiteral("c")).toArray());
        if (a && b && curve.size() >= 4 && (curve.size() - 1) % 3 == 0) {
            Spline spline;
            spline.points = curve;
            spline.start = unpair(saved.value(QStringLiteral("s")));
            spline.end = unpair(saved.value(QStringLiteral("e")));
            spline.labelPoint = unpair(saved.value(QStringLiteral("lp")));
            spline.fromAnchor = QPointF(a->x, a->y);
            spline.toAnchor = QPointF(b->x, b->y);
            l->spline = spline;
        } else if (a && b) {
            if (const std::optional<QPointF> lp = unpair(saved.value(QStringLiteral("lp"))))
                l->layoutLabelPoint = Link::ReservedLabel{*lp, QPointF(a->x, a->y), QPointF(b->x, b->y)};
        }
    }
    return unplaced;
}

QStringList layoutLines(const Board &board, const Codec &codec)
{
    const QString prefix = commentPrefix(codec) + QStringLiteral(" duet ");
    QStringList lines;
    for (const auto &s : board.shapes()) {
        if (s->key.isEmpty())
            continue;
        QJsonObject entry{{QStringLiteral("key"), s->key},
                          {QStringLiteral("x"), rounded(s->x)},
                          {QStringLiteral("y"), rounded(s->y)}};
        // A frame laid out as a cluster takes its size from the layout, not
        // from its content: measuring could not give it back.
        const bool laidOutSize = s->isGraphvizCluster();
        if (laidOutSize || (s->manualWidth && std::abs(*s->manualWidth - s->resizeMinWidth) > 0.5))
            entry.insert(QStringLiteral("w"), rounded(s->w));
        if (laidOutSize || (s->manualHeight && std::abs(*s->manualHeight - s->resizeMinHeight) > 0.5))
            entry.insert(QStringLiteral("h"), rounded(s->h));
        if (!s->customFill.isEmpty())
            entry.insert(QStringLiteral("fill"), s->customFill);
        lines.append(prefix + QString::fromUtf8(QJsonDocument(entry).toJson(QJsonDocument::Compact)));
    }
    QHash<QString, int> ranks;
    for (const auto &l : board.links()) {
        const QString key = linkKey(board, *l);
        const int rank = ranks.value(key);
        ranks.insert(key, rank + 1);
        QJsonObject entry{{QStringLiteral("link"), key}, {QStringLiteral("n"), rank}};
        if (!l->customLineStyle.isEmpty())
            entry.insert(QStringLiteral("style"), l->customLineStyle);
        if (!l->customColor.isEmpty())
            entry.insert(QStringLiteral("color"), l->customColor);
        if (l->customWidth)
            entry.insert(QStringLiteral("width"), *l->customWidth);
        const Shape *a = board.shape(l->from);
        const Shape *b = board.shape(l->to);
        if (a && b) {
            if (const Spline *spline = l->validSpline(*a, *b)) {
                entry.insert(QStringLiteral("c"), flat(spline->points));
                if (spline->start)
                    entry.insert(QStringLiteral("s"), pair(*spline->start));
                if (spline->end)
                    entry.insert(QStringLiteral("e"), pair(*spline->end));
                if (spline->labelPoint)
                    entry.insert(QStringLiteral("lp"), pair(*spline->labelPoint));
            } else if (const std::optional<QPointF> lp = l->validLayoutLabelPoint(*a, *b)) {
                entry.insert(QStringLiteral("lp"), pair(*lp));
            }
        }
        if (entry.size() > 2)
            lines.append(prefix + QString::fromUtf8(QJsonDocument(entry).toJson(QJsonDocument::Compact)));
    }
    if (!lines.isEmpty())
        lines.prepend(commentPrefix(codec) + QStringLiteral(" Layout kept by Duet (positions, sizes, colors, curves)"));
    return lines;
}

bool build(Board &board, const Codec &codec, const QString &source, const SavedLayout &layout, Loaded &loaded,
           QString *error, bool keepHistory = false)
{
    Model model;
    QString message;
    if (!codec.parse(source, model, &message)) {
        if (error)
            *error = message.isEmpty() ? QStringLiteral("The diagram cannot be read") : message;
        return false;
    }
    QSet<QString> unplacedKeys;
    for (const Model::Node &node : model.nodes) {
        if (!node.props.contains(QStringLiteral("x")) || !node.props.contains(QStringLiteral("y")))
            unplacedKeys.insert(node.key);
    }
    buildBoard(board, codec, model);
    loaded.codec = &codec;
    loaded.warnings = model.warnings;
    loaded.needsArrange = false;
    loaded.unplaced.clear();
    loaded.needsRouting = false;
    const Family &family = codec.family();
    QStringList unplaced;
    if (codec.sourceOwnsLayout()) {
        for (const auto &s : board.shapes()) {
            if (unplacedKeys.contains(s->key))
                unplaced.append(s->id);
        }
    } else {
        unplaced = restore(board, layout);
    }
    if (!board.isEmpty()) {
        if (unplaced.size() == qsizetype(board.shapes().size())
            || (family.alwaysArrange && codec.arrangeWithGraphviz() && !codec.sourceOwnsLayout()))
            loaded.needsArrange = true;
        else
            loaded.unplaced = unplaced;
    }
    if (!loaded.needsArrange && family.routeLinksOnMove) {
        for (const auto &l : board.links()) {
            if (!l->spline && l->from != l->to)
                loaded.needsRouting = true;
        }
    }
    if (!keepHistory)
        board.clearHistory();
    return true;
}

} // namespace

bool load(Board &board, const QString &path, const QByteArray &content, Loaded &loaded, QString *error)
{
    const QString language = languageOfFile(path);
    if (language.isEmpty()) {
        if (error)
            *error = QStringLiteral("Not a diagram file");
        return false;
    }
    SavedLayout layout;
    QString source = splitLayout(QString::fromUtf8(content), layout);
    // A blank file is a new diagram of the first family written in that
    // language.
    const Codec *codec = codecFor(language, source);
    if (!codec && source.trimmed().isEmpty()) {
        // The first family in menu order (the flowchart for Mermaid).
        for (const Family *family : allFamilies()) {
            for (const Codec *candidate : allCodecs()) {
                if (!codec && &candidate->family() == family && candidate->language() == language) {
                    codec = candidate;
                    source = candidate->blank();
                }
            }
        }
    }
    if (!codec) {
        if (error)
            *error = language == QLatin1String("mermaid")
                         ? QStringLiteral("Unsupported Mermaid diagram (the first line names its kind: flowchart, "
                                          "sequenceDiagram, classDiagram…)")
                         : QStringLiteral("Unsupported diagram");
        return false;
    }
    return build(board, *codec, source, layout, loaded, error);
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
                text = text.left(m.capturedStart()) + QStringLiteral("\n\n") + lines.join(QLatin1Char('\n'))
                       + QStringLiteral("\n@enduml");
            else
                text += QStringLiteral("\n\n") + lines.join(QLatin1Char('\n'));
        }
    }
    if (!text.endsWith(QLatin1Char('\n')))
        text += QLatin1Char('\n');
    return text.toUtf8();
}

bool applySource(Board &board, const Codec &codec, const QString &text, Loaded &loaded, QString *error)
{
    // The current arrangement is the layout to keep.
    SavedLayout layout;
    splitLayout(QString::fromUtf8(save(board, codec)), layout);
    SavedLayout ignored;
    const QString cleaned = splitLayout(text, ignored);
    const Codec *target = codecFor(codec.language(), cleaned);
    if (!target) {
        if (error)
            *error = QStringLiteral("This is not a diagram Duet reads");
        return false;
    }
    Board scratch;
    Loaded check;
    if (!build(scratch, *target, cleaned, layout, check, error))
        return false;
    const QByteArray before = board.snapshot();
    build(board, *target, cleaned, layout, loaded, error, true);
    board.pushHistory(before);
    return true;
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
    Loaded loaded;
    QString error;
    if (!load(board, path, content, loaded, &error))
        return {error};
    return loaded.warnings;
}

} // namespace diagram::document
