#include "Classes.h"

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/families/Families.h"

#include <QCoreApplication>
#include <QPainter>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <cmath>

namespace diagram::classes {

namespace {

const Font font(12.5);
const Font titleFont(14, 600);
const Font abstractTitleFont(14, 600, true);
constexpr qreal lineHeight = 20;
constexpr qreal pad = 14;

QString tr(const char *text)
{
    return QCoreApplication::translate("ClassDiagram", text);
}

// UML_CLASSIFIER_PRESETS: what the creation menu offers, and the stereotypes
// the inspector proposes.
struct Classifier {
    const char *id;
    const char *label;
    const char *defaultName;
    const char *stereotype; // null: a plain class
};

const auto classifiers = std::to_array<Classifier>({
    {"class", QT_TRANSLATE_NOOP("ClassDiagram", "Class"), QT_TRANSLATE_NOOP("ClassDiagram", "Class"), nullptr},
    {"abstract", QT_TRANSLATE_NOOP("ClassDiagram", "Abstract class"),
     QT_TRANSLATE_NOOP("ClassDiagram", "Abstract class"), "abstract"},
    {"interface", QT_TRANSLATE_NOOP("ClassDiagram", "Interface"), QT_TRANSLATE_NOOP("ClassDiagram", "Interface"),
     "interface"},
    {"enumeration", QT_TRANSLATE_NOOP("ClassDiagram", "Enumeration"), QT_TRANSLATE_NOOP("ClassDiagram", "Enumeration"),
     "enumeration"},
    {"datatype", QT_TRANSLATE_NOOP("ClassDiagram", "Data type"), QT_TRANSLATE_NOOP("ClassDiagram", "Data type"),
     "datatype"},
    {"primitive", QT_TRANSLATE_NOOP("ClassDiagram", "Primitive type"),
     QT_TRANSLATE_NOOP("ClassDiagram", "Primitive type"), "primitive"},
    {"signal", QT_TRANSLATE_NOOP("ClassDiagram", "Signal"), QT_TRANSLATE_NOOP("ClassDiagram", "Signal"), "signal"},
    {"metaclass", QT_TRANSLATE_NOOP("ClassDiagram", "Metaclass"), QT_TRANSLATE_NOOP("ClassDiagram", "Metaclass"),
     "metaclass"},
    {"annotation", QT_TRANSLATE_NOOP("ClassDiagram", "Annotation"), QT_TRANSLATE_NOOP("ClassDiagram", "Annotation"),
     "annotation"},
    {"entity", QT_TRANSLATE_NOOP("ClassDiagram", "Entity"), QT_TRANSLATE_NOOP("ClassDiagram", "Entity"), "entity"},
    {"struct", QT_TRANSLATE_NOOP("ClassDiagram", "Structure"), QT_TRANSLATE_NOOP("ClassDiagram", "Structure"),
     "struct"},
    {"exception", QT_TRANSLATE_NOOP("ClassDiagram", "Exception"), QT_TRANSLATE_NOOP("ClassDiagram", "Exception"),
     "exception"},
    {"protocol", QT_TRANSLATE_NOOP("ClassDiagram", "Protocol"), QT_TRANSLATE_NOOP("ClassDiagram", "Protocol"),
     "protocol"},
    {"custom", QT_TRANSLATE_NOOP("ClassDiagram", "Custom stereotype"), QT_TRANSLATE_NOOP("ClassDiagram", "UML element"),
     "stereotype"},
});

// UML_RELATION_PRESETS, with UML_RELATION_DEFAULT_LABELS in English.
struct Relation {
    const char *kind;
    const char *label;     // the menu entry, with its marker drawn in text
    const char *edgeLabel; // the label the new link gets
    const char *help;
};

const auto relations = std::to_array<Relation>({
    {"link", QT_TRANSLATE_NOOP("ClassDiagram", "Association —"), QT_TRANSLATE_NOOP("ClassDiagram", "Association"),
     nullptr},
    {"association", QT_TRANSLATE_NOOP("ClassDiagram", "Directed association →"),
     QT_TRANSLATE_NOOP("ClassDiagram", "Directed association"), nullptr},
    {"extension", QT_TRANSLATE_NOOP("ClassDiagram", "Generalization / inheritance ▷"),
     QT_TRANSLATE_NOOP("ClassDiagram", "Inheritance"),
     QT_TRANSLATE_NOOP("ClassDiagram", "Drag from the child class to the parent class.")},
    {"realization", QT_TRANSLATE_NOOP("ClassDiagram", "Realization / implementation ⇢▷"),
     QT_TRANSLATE_NOOP("ClassDiagram", "Implementation"),
     QT_TRANSLATE_NOOP("ClassDiagram", "Drag from the implementation to the interface.")},
    {"composition", QT_TRANSLATE_NOOP("ClassDiagram", "Composition ◆"),
     QT_TRANSLATE_NOOP("ClassDiagram", "Composition"),
     QT_TRANSLATE_NOOP("ClassDiagram", "Drag from the part to the whole that owns it.")},
    {"aggregation", QT_TRANSLATE_NOOP("ClassDiagram", "Aggregation ◇"),
     QT_TRANSLATE_NOOP("ClassDiagram", "Aggregation"),
     QT_TRANSLATE_NOOP("ClassDiagram", "Drag from the part to the whole.")},
    {"dependency", QT_TRANSLATE_NOOP("ClassDiagram", "Dependency ⇢"), QT_TRANSLATE_NOOP("ClassDiagram", "Dependency"),
     nullptr},
    {"dotted", QT_TRANSLATE_NOOP("ClassDiagram", "Dotted link ⋯"), QT_TRANSLATE_NOOP("ClassDiagram", "Link"), nullptr},
});

// The kinds as the inspector names them: the menu entries without their
// text markers.
QString kindName(const char *kind)
{
    const QString k = QString::fromLatin1(kind);
    if (k == QLatin1String("link"))
        return tr("Association");
    if (k == QLatin1String("association"))
        return tr("Directed association");
    if (k == QLatin1String("extension"))
        return tr("Generalization / inheritance");
    if (k == QLatin1String("realization"))
        return tr("Realization / implementation");
    if (k == QLatin1String("composition"))
        return tr("Composition");
    if (k == QLatin1String("aggregation"))
        return tr("Aggregation");
    if (k == QLatin1String("dependency"))
        return tr("Dependency");
    return tr("Dotted link");
}

QJsonValue stereotypeValue(const QString &stereotype)
{
    return stereotype.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(stereotype);
}

// The class name of a saved shape: `name`, else the `text` of an older
// save, else the default.
QString loadedName(const QJsonObject &props)
{
    const QJsonValue name = props.value(QStringLiteral("name"));
    if (name.isString())
        return name.toString();
    const QJsonValue text = props.value(QStringLiteral("text"));
    if (text.isString())
        return text.toString();
    return tr("Class");
}

// The outlines of the relation markers, with their tip at `tip`, pointing
// along `angle`, `size` long.

QPainterPath trianglePath(QPointF tip, qreal angle, qreal size)
{
    const qreal bx = tip.x();
    const qreal by = tip.y();
    const qreal spread = 0.46;
    return polygon({{bx, by},
                    {bx - size * std::cos(angle - spread), by - size * std::sin(angle - spread)},
                    {bx - size * std::cos(angle + spread), by - size * std::sin(angle + spread)}});
}

// `size` is half the diamond's length.
QPainterPath diamondPath(QPointF tip, qreal angle, qreal size)
{
    const qreal bx = tip.x();
    const qreal by = tip.y();
    const qreal mx = bx - size * std::cos(angle);
    const qreal my = by - size * std::sin(angle);
    const qreal side = size * 0.55;
    return polygon({{bx, by},
                    {mx + side * std::cos(angle + M_PI / 2), my + side * std::sin(angle + M_PI / 2)},
                    {bx - 2 * size * std::cos(angle), by - 2 * size * std::sin(angle)},
                    {mx + side * std::cos(angle - M_PI / 2), my + side * std::sin(angle - M_PI / 2)}});
}

QPainterPath openArrowPath(QPointF tip, qreal angle, qreal size)
{
    const qreal bx = tip.x();
    const qreal by = tip.y();
    const qreal spread = 0.5;
    QPainterPath head;
    head.moveTo(bx - size * std::cos(angle - spread), by - size * std::sin(angle - spread));
    head.lineTo(bx, by);
    head.lineTo(bx - size * std::cos(angle + spread), by - size * std::sin(angle + spread));
    return head;
}

} // namespace

QString defaultRelationLabel(const QString &kind)
{
    for (const Relation &relation : relations) {
        if (kind == QLatin1String(relation.kind))
            return tr(relation.edgeLabel);
    }
    return tr("Relationship");
}

// ---------------------------------------------------------------------------
// ClassShape

ClassShape::ClassShape()
    : Shape(QStringLiteral("class"), 180, 60)
    , name(tr("Class"))
{
}

void ClassShape::load(const QJsonObject &props)
{
    Shape::load(props);
    name = loadedName(props);
    stereotype = jsonString(props, QStringLiteral("stereotype"));
    attributes = jsonStrings(props, QStringLiteral("attributes"));
    methods = jsonStrings(props, QStringLiteral("methods"));
}

QJsonObject ClassShape::toJson() const
{
    QJsonObject json = Shape::toJson();
    json.insert(QStringLiteral("name"), name);
    json.insert(QStringLiteral("stereotype"), stereotypeValue(stereotype));
    json.insert(QStringLiteral("attributes"), toJsonArray(attributes));
    json.insert(QStringLiteral("methods"), toJsonArray(methods));
    return json;
}

void ClassShape::measure(const Board &)
{
    // Width follows the longest member line; height follows the member
    // count. UML members are never wrapped when drawn: the box really
    // follows the longest signature instead of cutting it.
    qreal widest = text::width(titleFont, name);
    if (!stereotype.isEmpty())
        widest = std::max(widest, text::width(titleFont, QStringLiteral("«%1»").arg(stereotype)));
    for (const QString &member : attributes + methods)
        widest = std::max(widest, text::width(font, member));
    w = std::ceil(std::max<qreal>(150, widest + pad * 2));
    const qreal header = (stereotype.isEmpty() ? 1 : 2) * lineHeight + pad * 2;
    const auto section = [](qsizetype count) { return count ? count * lineHeight + pad : pad; };
    h = header + section(attributes.size()) + section(methods.size());
}

QString ClassShape::editText() const
{
    QStringList lines;
    if (!stereotype.isEmpty())
        lines.append(QStringLiteral("<<%1>>").arg(stereotype));
    lines.append(name);
    lines.append(QStringLiteral("--"));
    lines += attributes;
    lines.append(QStringLiteral("--"));
    lines += methods;
    return lines.join(QLatin1Char('\n'));
}

void ClassShape::setEditText(const QString &value)
{
    std::array<QStringList, 3> sections;
    int at = 0;
    for (const QString &raw : value.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line == QLatin1String("--")) {
            at = std::min(at + 1, 2);
            continue;
        }
        if (!line.isEmpty())
            sections.at(at).append(line);
    }
    static const QRegularExpression stereotypeLine(QStringLiteral("^<<(.+)>>$"));
    stereotype.clear();
    name = tr("Class");
    for (const QString &line : sections[0]) {
        const QRegularExpressionMatch m = stereotypeLine.match(line);
        if (m.hasMatch())
            stereotype = m.captured(1);
        else
            name = line;
    }
    attributes = sections[1];
    methods = sections[2];
}

void ClassShape::onDoubleClick(Board &board)
{
    // The whole class is edited as one text: Enter is a new line, the `--`
    // lines separate the sections.
    TextEditSpec spec;
    spec.get = [this] { return editText(); };
    spec.set = [this](const QString &value) { setEditText(value); };
    spec.color = board.style().palette.ink;
    spec.font = font;
    spec.lineHeight = lineHeight;
    spec.align = Align::Left;
    spec.rect = [this] { return QRectF(x + pad, y + pad, w - pad * 2, h - pad * 2); };
    spec.enterCommits = false;
    board.beginTextEdit(this, spec);
}

void ClassShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QPainterPath path = roundRect(x, y, w, h, shapeRadiusSm);
    shadowFill(painter, path, fillColor(style.tones.cool), style);
    stroke(painter, path, strokeColor(state.selected ? style.shape.selectedStroke : style.palette.border),
           state.selected ? 2 : shapeBorderWidth);
    if (state.editing)
        return;

    painter.save();
    painter.setClipPath(path, Qt::IntersectClip);
    qreal ty = y + pad;
    const QColor ink = textColor(style.palette.ink);
    if (!stereotype.isEmpty()) {
        text::draw(painter, font, QStringLiteral("«%1»").arg(stereotype), QPointF(cx(), ty),
                   textColor(style.palette.inkMuted), Align::Center, Baseline::Top);
        ty += lineHeight;
    }
    text::draw(painter, stereotype == QLatin1String("abstract") ? abstractTitleFont : titleFont, name,
               QPointF(cx(), ty), ink, Align::Center, Baseline::Top);
    ty += lineHeight + pad;

    const auto separator = [&] {
        strokeLine(painter, QPointF(x, ty - pad / 2), QPointF(x + w, ty - pad / 2), style.palette.separator, 1);
    };
    separator();
    ty += pad / 2;
    for (const QString &member : attributes) {
        text::draw(painter, font, member, QPointF(x + pad, ty), ink, Align::Left, Baseline::Top);
        ty += lineHeight;
    }
    ty += pad / 2;
    separator();
    ty += pad / 2;
    for (const QString &member : methods) {
        text::draw(painter, font, member, QPointF(x + pad, ty), ink, Align::Left, Baseline::Top);
        ty += lineHeight;
    }
    painter.restore();
}

QList<Property> ClassShape::properties() const
{
    QList<std::pair<QString, QString>> choices{{QString(), tr("None")}};
    bool known = stereotype.isEmpty();
    for (const Classifier &classifier : classifiers) {
        if (!classifier.stereotype)
            continue;
        const QString value = QString::fromUtf8(classifier.stereotype);
        choices.append({value, tr(classifier.label)});
        known = known || value == stereotype;
    }
    // A stereotype read from a source (PlantUML's `enum`, a «service») stays
    // selectable.
    if (!known)
        choices.append({stereotype, QStringLiteral("«%1»").arg(stereotype)});
    return {{QStringLiteral("stereotype"), tr("Stereotype"), Property::Kind::Choice, choices}};
}

QVariant ClassShape::propertyValue(const QString &propertyId) const
{
    if (propertyId == QLatin1String("stereotype"))
        return stereotype;
    return {};
}

void ClassShape::setPropertyValue(const QString &propertyId, const QVariant &value)
{
    if (propertyId == QLatin1String("stereotype"))
        stereotype = value.toString().trimmed();
}

// ---------------------------------------------------------------------------
// ClassLink

ClassLink::ClassLink()
    : Link(QStringLiteral("relation"))
{
}

void ClassLink::load(const QJsonObject &props)
{
    Link::load(props);
    kind = jsonString(props, QStringLiteral("kind"), QStringLiteral("association"));
    if (kind.isEmpty())
        kind = QStringLiteral("association");
    // A relation created without a label gets its kind's name; an explicit
    // one, even empty, is kept.
    label = props.contains(QStringLiteral("label")) ? label.trimmed() : defaultRelationLabel(kind);
    fromCard = jsonString(props, QStringLiteral("fromCard"));
    toCard = jsonString(props, QStringLiteral("toCard"));
}

QJsonObject ClassLink::toJson() const
{
    QJsonObject json = Link::toJson();
    json.insert(QStringLiteral("kind"), kind);
    if (!fromCard.isEmpty())
        json.insert(QStringLiteral("fromCard"), fromCard);
    if (!toCard.isEmpty())
        json.insert(QStringLiteral("toCard"), toCard);
    return json;
}

QStringList ClassLink::graphvizAttributes(const Board &board) const
{
    // Graphviz reserves the room of the marker drawn: arrowsize is the
    // marker's length over Graphviz's 10-point arrow.
    const qreal standard = board.style().link.arrow + 2;
    qreal markerLength = 0;
    if (kind == QLatin1String("extension") || kind == QLatin1String("realization"))
        markerLength = 14;
    else if (kind == QLatin1String("composition") || kind == QLatin1String("aggregation"))
        markerLength = 18;
    else if (kind == QLatin1String("association") || kind == QLatin1String("dependency"))
        markerLength = standard;
    if (!markerLength)
        return {QStringLiteral("arrowhead=none")};
    return {QStringLiteral("arrowsize=") + QString::number(markerLength / 10, 'f', 3)};
}

void ClassLink::draw(QPainter &painter, const Board &board, bool selected) const
{
    const std::optional<Route> r = route(board);
    if (!r)
        return;
    const LinkStyle style = drawStyle(board, selected);
    const qreal angle = arrivalAngle(*r);
    const bool dashed =
        kind == QLatin1String("realization") || kind == QLatin1String("dependency") || kind == QLatin1String("dotted");
    // The body stops at the base of a closed marker (triangle, diamond); the
    // open arrow of associations is drawn over the line's end.
    qreal markerLength = 0;
    if (kind == QLatin1String("extension") || kind == QLatin1String("realization"))
        markerLength = arrowBaseInset(14, 0.46);
    else if (kind == QLatin1String("composition") || kind == QLatin1String("aggregation"))
        markerLength = 18;
    strokeBody(
        painter, *r,
        {style.color, style.width, dashed ? QList<qreal>{6, 4} : QList<qreal>{}, markerLength * style.markerScale, 0});
    drawMarker(painter, board, *r, style);
    drawCards(painter, board, *r, angle);
}

void ClassLink::drawMarker(QPainter &painter, const Board &board, const Route &r, const LinkStyle &style) const
{
    const qreal angle = arrivalAngle(r);
    const QColor background = board.style().background;
    if (kind == QLatin1String("extension") || kind == QLatin1String("realization")) {
        const QPainterPath head = trianglePath(r.b, angle, 14 * style.markerScale);
        painter.fillPath(head, background);
        stroke(painter, head, style.color, style.width);
    } else if (kind == QLatin1String("composition") || kind == QLatin1String("aggregation")) {
        // Filled for a composition, hollow for an aggregation.
        const QPainterPath diamond = diamondPath(r.b, angle, 9 * style.markerScale);
        painter.fillPath(diamond, kind == QLatin1String("composition") ? style.color : background);
        stroke(painter, diamond, style.color, style.width);
    } else if (kind == QLatin1String("association") || kind == QLatin1String("dependency")) {
        const qreal size = (board.style().link.arrow + 2) * style.markerScale;
        stroke(painter, openArrowPath(r.b, angle, size), style.color, style.width);
    }
}

void ClassLink::drawCards(QPainter &painter, const Board &board, const Route &r, qreal angle) const
{
    // Cardinalities near each end, set back along the line and offset
    // perpendicular to it.
    if (fromCard.isEmpty() && toCard.isEmpty())
        return;
    const auto &o = board.style().linkLabel;
    const auto place = [&](const QString &card, QPointF at, qreal sign) {
        if (card.isEmpty())
            return;
        const qreal back = 18 * sign;
        const QPointF p(at.x() - back * std::cos(angle) + 12 * std::cos(angle - M_PI / 2),
                        at.y() - back * std::sin(angle) + 12 * std::sin(angle - M_PI / 2));
        text::draw(painter, Font(o.fontPx), card, p, o.color, Align::Center, Baseline::Middle);
    };
    place(fromCard, r.a, -1);
    place(toCard, r.b, 1);
}

QList<Property> ClassLink::properties() const
{
    QList<std::pair<QString, QString>> kinds;
    for (const Relation &relation : relations)
        kinds.append({QString::fromLatin1(relation.kind), kindName(relation.kind)});
    return {
        {QStringLiteral("kind"), tr("Relationship"), Property::Kind::Choice, kinds},
        {QStringLiteral("fromCard"), tr("Cardinality at the source"), Property::Kind::Text, {}},
        {QStringLiteral("toCard"), tr("Cardinality at the target"), Property::Kind::Text, {}},
    };
}

QVariant ClassLink::propertyValue(const QString &propertyId) const
{
    if (propertyId == QLatin1String("kind"))
        return kind;
    if (propertyId == QLatin1String("fromCard"))
        return fromCard;
    if (propertyId == QLatin1String("toCard"))
        return toCard;
    return {};
}

void ClassLink::setPropertyValue(const QString &propertyId, const QVariant &value)
{
    if (propertyId == QLatin1String("kind")) {
        const QString next = value.toString();
        // A label that only named the old kind follows the new one; a
        // label of the user's own stays.
        if (label == defaultRelationLabel(kind))
            label = defaultRelationLabel(next);
        kind = next;
    } else if (propertyId == QLatin1String("fromCard")) {
        fromCard = value.toString().trimmed();
    } else if (propertyId == QLatin1String("toCard")) {
        toCard = value.toString().trimmed();
    }
}

} // namespace diagram::classes

// ---------------------------------------------------------------------------
// The family

namespace diagram::families {

const Family &classes()
{
    using namespace diagram::classes;
    static const Family family = [] {
        Family f;
        f.id = QStringLiteral("classes");
        f.title = tr("Class diagram");
        f.shapes = {{QStringLiteral("class"), [] { return std::unique_ptr<Shape>(new ClassShape); }}};
        // Several relations may join the same two classes (a dependency
        // beside an association).
        f.links = {{QStringLiteral("relation"), [] { return std::unique_ptr<Link>(new ClassLink); }, false, true}};
        f.defaultShape = QStringLiteral("class");
        f.defaultLink = QStringLiteral("relation");
        for (const Classifier &classifier : classifiers) {
            f.elements.append(
                ElementPreset{QString::fromLatin1(classifier.id),
                              tr(classifier.label),
                              {},
                              QStringLiteral("class"),
                              QJsonObject{{QStringLiteral("name"), tr(classifier.defaultName)},
                                          {QStringLiteral("stereotype"),
                                           classifier.stereotype ? QJsonValue(QString::fromUtf8(classifier.stereotype))
                                                                 : QJsonValue(QJsonValue::Null)}}});
        }
        for (const Relation &relation : relations) {
            f.relations.append(RelationPreset{QString::fromLatin1(relation.kind), tr(relation.label),
                                              relation.help ? tr(relation.help) : QString(),
                                              QJsonObject{{QStringLiteral("kind"), QString::fromLatin1(relation.kind)},
                                                          {QStringLiteral("label"), tr(relation.edgeLabel)}}});
        }
        f.cardinalities = true;
        return f;
    }();
    return family;
}

} // namespace diagram::families
