#include "C4.h"

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/Graphviz.h"
#include "viewers/diagram/families/Families.h"

#include <QCoreApplication>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace diagram::c4 {

namespace {

// The title a boundary paints above its frame. Graphviz reserves that band at
// the top of the cluster, so the two are stated once and read from here.
constexpr qreal boundaryTitlePx = 12;
// Width a relation panel wraps at, in the font it is actually painted with.
constexpr qreal relationMaxWidth = 220;
const Font font(13);
const Font small(11.5);
const Font title(15, 600);
const Font boundaryFont(boundaryTitlePx, 700);
constexpr qreal lineHeight = 20;
constexpr qreal databaseCapHeight = 26;
constexpr qreal databaseContentGap = 8;

QString tr(const char *text)
{
    return QCoreApplication::translate("C4Diagram", text);
}

QString defaultElementName()
{
    return tr("Element");
}

QString defaultBoundaryName()
{
    return tr("Boundary");
}

QString defaultRelationLabel()
{
    return tr("Uses");
}

const QList<std::pair<QString, QString>> &kindChoices()
{
    static const QList<std::pair<QString, QString>> list{
        {QStringLiteral("person"), tr("Person")},
        {QStringLiteral("system"), tr("Software system")},
        {QStringLiteral("container"), tr("Container")},
        {QStringLiteral("component"), tr("Component")},
    };
    return list;
}

} // namespace

QString kindTerm(const QString &kind)
{
    for (const auto &[value, label] : kindChoices()) {
        if (value == kind)
            return label;
    }
    return kind;
}

// ---------------------------------------------------------------------------
// C4ElementShape

C4ElementShape::C4ElementShape()
    : Shape(QStringLiteral("c4"), 220, 116)
{
}

void C4ElementShape::load(const QJsonObject &props)
{
    Shape::load(props);
    name = jsonString(props, QStringLiteral("name"), jsonString(props, QStringLiteral("text"), defaultElementName()));
    kind = jsonString(props, QStringLiteral("kind"), QStringLiteral("system"));
    description = jsonString(props, QStringLiteral("description"));
    technology = jsonString(props, QStringLiteral("technology"));
    external = jsonBool(props, QStringLiteral("external"));
    variant = jsonString(props, QStringLiteral("variant"), QStringLiteral("box"));
}

QJsonObject C4ElementShape::toJson() const
{
    QJsonObject json = Shape::toJson();
    json.insert(QStringLiteral("name"), name);
    json.insert(QStringLiteral("kind"), kind);
    json.insert(QStringLiteral("description"), description);
    json.insert(QStringLiteral("technology"), technology);
    json.insert(QStringLiteral("external"), external);
    json.insert(QStringLiteral("variant"), variant);
    return json;
}

C4ElementShape::BodyStyle C4ElementShape::bodyStyle(const Style &style) const
{
    const Palette &p = style.palette;
    const Tones &tones = style.tones;
    BodyStyle body;
    if (external)
        body = {tones.external, p.inkMuted, p.ink, p.inkMuted};
    else if (variant == QLatin1String("database") || variant == QLatin1String("queue"))
        body = {tones.data, p.border, p.ink, p.inkMuted};
    else if (kind == QLatin1String("system"))
        body = {tones.strong, p.border, QColor(Qt::white), p.separator};
    else if (kind == QLatin1String("container"))
        body = {tones.cool, p.border, p.ink, p.inkMuted};
    else if (kind == QLatin1String("component"))
        body = {tones.sage, p.line, p.ink, p.inkMuted};
    else
        body = {tones.warm, p.border, p.ink, p.inkMuted};
    if (!customFill.isEmpty()) {
        const QColor ink = textColor(body.ink);
        return {fillColor(body.fill), strokeColor(body.stroke), ink, ink};
    }
    return body;
}

QString C4ElementShape::typeText() const
{
    const QString suffix = variant == QLatin1String("database") ? QStringLiteral(" · ") + tr("Database")
                           : variant == QLatin1String("queue")  ? QStringLiteral(" · ") + tr("Queue")
                                                                : QString();
    return QLatin1Char('[') + kindTerm(kind) + suffix + QLatin1Char(']');
}

qreal C4ElementShape::contentTop() const
{
    if (kind == QLatin1String("person"))
        return 84;
    if (variant == QLatin1String("database"))
        return databaseCapHeight + databaseContentGap;
    return 20;
}

void C4ElementShape::measure(const Board &)
{
    // Every line is measured in the font it is painted with: the widest of
    // them sets the width, between 210 and 300.
    constexpr qreal maxInner = 260;
    qreal widest = 0;
    for (const QString &line : text::wrap(title, name, maxInner))
        widest = std::max(widest, text::width(title, line));
    for (const QString &line : text::wrap(small, typeText(), maxInner))
        widest = std::max(widest, text::width(small, line));
    for (const QString &line : text::wrap(small, technology, maxInner))
        widest = std::max(widest, text::width(small, line));
    for (const QString &line : text::wrap(font, description, maxInner))
        widest = std::max(widest, text::width(font, line));
    w = std::min<qreal>(300, std::max<qreal>(210, widest + 40));
    h = layoutText();
}

qreal C4ElementShape::layoutText()
{
    const qreal inner = std::max<qreal>(20, w - 40);
    titleLines_ = text::wrap(title, name, inner);
    typeLines_ = text::wrap(small, typeText(), inner);
    technologyLines_ =
        technology.trimmed().isEmpty() ? QStringList() : text::wrap(small, technology, std::max<qreal>(20, inner - 12));
    descriptionLines_ = description.trimmed().isEmpty() ? QStringList() : text::wrap(font, description, inner);
    return std::max<qreal>(104, contentTop() + titleLines_.size() * 22 + 4 + typeLines_.size() * 16 + 10 +
                                    (technologyLines_.isEmpty() ? 0 : technologyLines_.size() * 15 + 11) +
                                    descriptionLines_.size() * lineHeight + 20);
}

void C4ElementShape::resizeTo(qreal width, qreal height, const Board &board)
{
    Shape::resizeTo(width, height, board);
    // Narrower, the texts wrap on more lines: the box never gets shorter than
    // they are.
    h = std::max(h, layoutText());
}

void C4ElementShape::onDoubleClick(Board &board)
{
    // One editor for the three texts: the name, the technology, then the
    // description on the following lines.
    TextEditSpec spec;
    spec.get = [this] { return QStringList{name, technology, description}.join(QLatin1Char('\n')); };
    spec.set = [this](const QString &value) {
        QStringList parts = value.split(QLatin1Char('\n'));
        const QString first = parts.isEmpty() ? QString() : parts.takeFirst().trimmed();
        name = first.isEmpty() ? defaultElementName() : first;
        technology = parts.isEmpty() ? QString() : parts.takeFirst().trimmed();
        description = parts.join(QLatin1Char('\n')).trimmed();
    };
    spec.color = bodyStyle(board.style()).ink;
    spec.font = font;
    spec.lineHeight = lineHeight;
    spec.enterCommits = false;
    spec.rect = [this] {
        const qreal top = variant == QLatin1String("database") ? contentTop() - 2 : 20;
        return QRectF(x + 20, y + top, w - 40, h - top - 20);
    };
    board.beginTextEdit(this, spec);
}

QPainterPath C4ElementShape::boxPath() const
{
    if (variant == QLatin1String("database")) {
        constexpr qreal cap = 13;
        QPainterPath path;
        path.moveTo(x, y + cap);
        path.cubicTo(x, y - 3, x + w, y - 3, x + w, y + cap);
        path.lineTo(x + w, y + h - cap);
        path.cubicTo(x + w, y + h + 3, x, y + h + 3, x, y + h - cap);
        path.closeSubpath();
        return path;
    }
    return roundRect(x, y, w, h, ShapeRadiusSm);
}

void C4ElementShape::drawPerson(QPainter &painter, const QColor &ink) const
{
    const qreal headY = y + 43;
    QPainterPath head = ellipse(QPointF(cx(), headY - 14), 14, 14);
    painter.fillPath(head, ink);
    // The shoulders: the upper half of a circle, closed by a flat base.
    QPainterPath body;
    body.arcMoveTo(QRectF(cx() - 31, headY + 21 - 31, 62, 62), 180);
    body.arcTo(QRectF(cx() - 31, headY + 21 - 31, 62, 62), 180, -180);
    body.lineTo(cx() + 31, headY + 33);
    body.lineTo(cx() - 31, headY + 33);
    body.closeSubpath();
    painter.fillPath(body, ink);
}

void C4ElementShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const BodyStyle body = bodyStyle(style);
    const QPainterPath path = boxPath();
    shadowFill(painter, path, body.fill, style);
    stroke(painter, path, strokeColor(state.selected ? style.shape.selectedStroke : body.stroke),
           state.selected ? 2 : ShapeBorderWidth, external ? QList<qreal>{7, 4} : QList<qreal>{});

    if (variant == QLatin1String("database")) {
        stroke(painter, ellipse(QPointF(cx(), y + 13), w / 2, 13), body.stroke, 1.25);
    } else if (variant == QLatin1String("queue")) {
        for (const qreal offset : {qreal(13), w - 13})
            strokeLine(painter, QPointF(x + offset, y + 12), QPointF(x + offset, y + h - 12), body.stroke, 1);
    }
    if (state.editing)
        return;

    // A last safety net: even if a font's metrics differ slightly from the
    // measure, no glyph can physically leave the silhouette.
    painter.save();
    painter.setClipPath(path, Qt::IntersectClip);
    const bool centered = kind == QLatin1String("person") || variant == QLatin1String("database");
    const qreal textX = centered ? cx() : x + 20;
    const Align align = centered ? Align::Center : Align::Left;
    qreal ty = y + contentTop();
    if (kind == QLatin1String("person"))
        drawPerson(painter, body.ink);
    for (const QString &line : titleLines_) {
        text::draw(painter, title, line, QPointF(textX, ty), body.ink, align, Baseline::Top);
        ty += 22;
    }
    ty += 4;
    for (const QString &line : typeLines_) {
        text::draw(painter, small, line, QPointF(textX, ty), body.muted, align, Baseline::Top);
        ty += 16;
    }
    ty += 8;
    if (!technologyLines_.isEmpty()) {
        // The technology sits in a translucent pill.
        const qreal pillH = technologyLines_.size() * 15 + 7;
        const qreal tw = std::min(w - 24, text::widest(small, technologyLines_) + 18);
        const bool strong = kind == QLatin1String("system") && !external;
        const QPainterPath pill = roundRect(centered ? cx() - tw / 2 : textX - 6, ty - 2, tw, pillH, 3);
        painter.fillPath(pill, withAlpha(strong ? style.palette.labelBg : style.palette.fill, strong ? 0.18 : 0.45));
        qreal py = ty + 1;
        for (const QString &line : technologyLines_) {
            text::draw(painter, small, line, QPointF(textX, py), body.muted, align, Baseline::Top);
            py += 15;
        }
        ty += pillH + 4;
    }
    for (const QString &line : descriptionLines_) {
        text::draw(painter, font, line, QPointF(textX, ty), body.ink, align, Baseline::Top);
        ty += lineHeight;
    }
    painter.restore();
}

QList<Property> C4ElementShape::properties() const
{
    return {
        {QStringLiteral("kind"), tr("Kind"), Property::Kind::Choice, kindChoices()},
        {QStringLiteral("variant"),
         tr("Shape"),
         Property::Kind::Choice,
         {{QStringLiteral("box"), tr("Box")},
          {QStringLiteral("database"), tr("Database")},
          {QStringLiteral("queue"), tr("Queue")}}},
        {QStringLiteral("external"), tr("External"), Property::Kind::Bool, {}},
        {QStringLiteral("technology"), tr("Technology"), Property::Kind::Text, {}},
        {QStringLiteral("description"), tr("Description"), Property::Kind::Lines, {}},
    };
}

QVariant C4ElementShape::propertyValue(const QString &id) const
{
    if (id == QLatin1String("kind"))
        return kind;
    if (id == QLatin1String("variant"))
        return variant;
    if (id == QLatin1String("external"))
        return external;
    if (id == QLatin1String("technology"))
        return technology;
    if (id == QLatin1String("description"))
        return description;
    return {};
}

void C4ElementShape::setPropertyValue(const QString &id, const QVariant &value)
{
    if (id == QLatin1String("kind"))
        kind = value.toString();
    else if (id == QLatin1String("variant"))
        variant = value.toString();
    else if (id == QLatin1String("external"))
        external = value.toBool();
    else if (id == QLatin1String("technology"))
        technology = value.toString();
    else if (id == QLatin1String("description"))
        description = value.toString();
}

// ---------------------------------------------------------------------------
// C4BoundaryShape

C4BoundaryShape::C4BoundaryShape()
    : Shape(QStringLiteral("c4boundary"), 380, 240)
{
}

void C4BoundaryShape::load(const QJsonObject &props)
{
    Shape::load(props);
    name = jsonString(props, QStringLiteral("name"), defaultBoundaryName());
    boundaryKind = jsonString(props, QStringLiteral("boundaryKind"), QStringLiteral("System_Boundary"));
}

QJsonObject C4BoundaryShape::toJson() const
{
    QJsonObject json = Shape::toJson();
    json.insert(QStringLiteral("name"), name);
    json.insert(QStringLiteral("boundaryKind"), boundaryKind);
    return json;
}

QString C4BoundaryShape::kindLabel() const
{
    if (boundaryKind == QLatin1String("Enterprise_Boundary"))
        return tr("Enterprise");
    if (boundaryKind == QLatin1String("Container_Boundary"))
        return tr("Container");
    if (boundaryKind == QLatin1String("Deployment_Node"))
        return tr("Deployment");
    return tr("Software system");
}

QString C4BoundaryShape::title() const
{
    return name + QStringLiteral("  ·  ") + kindLabel();
}

void C4BoundaryShape::measure(const Board &)
{
    // A boundary never shrinks under its contents' layout: it only keeps
    // room for its title.
    labelWidth_ = text::width(boundaryFont, title()) + 58;
    w = std::max({qreal(340), w, labelWidth_});
    h = std::max<qreal>(220, h);
}

bool C4BoundaryShape::hitTest(QPointF point) const
{
    // The inside of a boundary is a work area, not a huge handle: only its
    // outline and its title select it, so that a double-click in the free
    // space can create a child there.
    constexpr qreal tolerance = 9;
    const qreal wx = point.x(), wy = point.y();
    const bool inside = wx >= x && wx <= x + w && wy >= y && wy <= y + h;
    if (!inside)
        return wx >= x + 14 && wx <= x + std::max<qreal>(80, labelWidth_) && wy >= y - 15 && wy <= y + 15;
    return wx - x <= tolerance || x + w - wx <= tolerance || wy - y <= tolerance || y + h - wy <= tolerance;
}

void C4BoundaryShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QPainterPath frame = roundRect(x, y, w, h, ShapeRadiusSm);
    painter.save();
    // A painted boundary stays translucent: what it holds remains legible.
    painter.setOpacity(painter.opacity() * (customFill.isEmpty() ? 1 : 0.58));
    shadowFill(painter, frame, fillColor(style.tones.group), style);
    painter.restore();
    stroke(painter, frame, strokeColor(state.selected ? style.shape.selectedStroke : style.palette.border),
           state.selected ? 2 : ShapeBorderWidth, {9, 6});

    const QString label = title();
    const qreal width = text::width(boundaryFont, label) + 22;
    const QPainterPath pill = roundRect(x + 18, y - 12, width, 25, ShapeRadiusSm);
    painter.fillPath(pill, fillColor(style.palette.labelBg));
    stroke(painter, pill, strokeColor(style.palette.border), 1);
    if (state.editing)
        return;
    text::draw(painter, boundaryFont, label, QPointF(x + 29, y + 0.5), textColor(style.palette.ink), Align::Left,
               Baseline::Middle);
}

QString C4BoundaryShape::layoutLabel() const
{
    return name + QStringLiteral(" · ") + kindLabel();
}

qreal C4BoundaryShape::layoutLabelFontPx() const
{
    // The band Graphviz keeps free above the cluster is the one this
    // boundary paints, not Graphviz's own default size.
    return boundaryTitlePx;
}

std::optional<QSizeF> C4BoundaryShape::layoutMinimumSize() const
{
    // The floor this boundary takes back on the next measure, whatever
    // Graphviz derived from its contents alone.
    return QSizeF(std::max<qreal>(340, labelWidth_), 220);
}

void C4BoundaryShape::onDoubleClick(Board &board)
{
    TextEditSpec spec;
    spec.get = [this] { return name; };
    spec.set = [this](const QString &value) {
        const QString trimmed = value.trimmed();
        name = trimmed.isEmpty() ? defaultBoundaryName() : trimmed;
    };
    spec.color = board.style().palette.ink;
    spec.font = font;
    spec.lineHeight = 18;
    spec.rect = [this] { return QRectF(x + 18, y - 12, std::min<qreal>(260, w - 36), 25); };
    board.beginTextEdit(this, spec);
}

QList<Property> C4BoundaryShape::properties() const
{
    return {{QStringLiteral("boundaryKind"),
             tr("Kind"),
             Property::Kind::Choice,
             {{QStringLiteral("Enterprise_Boundary"), tr("Enterprise")},
              {QStringLiteral("System_Boundary"), tr("Software system")},
              {QStringLiteral("Container_Boundary"), tr("Container")},
              {QStringLiteral("Deployment_Node"), tr("Deployment")},
              {QStringLiteral("Boundary"), tr("Boundary")}}}};
}

QVariant C4BoundaryShape::propertyValue(const QString &id) const
{
    if (id == QLatin1String("boundaryKind"))
        return boundaryKind;
    return {};
}

void C4BoundaryShape::setPropertyValue(const QString &id, const QVariant &value)
{
    if (id == QLatin1String("boundaryKind"))
        boundaryKind = value.toString();
}

// ---------------------------------------------------------------------------
// C4RelationLink

C4RelationLink::C4RelationLink()
    : Link(QStringLiteral("c4relation"))
{
}

void C4RelationLink::load(const QJsonObject &props)
{
    Link::load(props);
    // A relation always says what it does.
    label = label.trimmed();
    if (label.isEmpty())
        label = defaultRelationLabel();
    technology = jsonString(props, QStringLiteral("technology"));
    description = jsonString(props, QStringLiteral("description"));
    bidirectional = jsonBool(props, QStringLiteral("bidirectional"));
}

QJsonObject C4RelationLink::toJson() const
{
    QJsonObject json = Link::toJson();
    json.insert(QStringLiteral("technology"), technology);
    json.insert(QStringLiteral("description"), description);
    json.insert(QStringLiteral("bidirectional"), bidirectional);
    return json;
}

QStringList C4RelationLink::displayLines(const Board &board) const
{
    // Wrapping happens in the font the panel is PAINTED with: wrapping in one
    // font and drawing in another gives Graphviz a different number of lines
    // than the canvas ends up showing.
    const Font labelFont(board.style().linkLabel.fontPx);
    QStringList lines;
    for (const QString &logical :
         {label, technology.isEmpty() ? QString() : QLatin1Char('[') + technology + QLatin1Char(']')}) {
        if (!logical.isEmpty())
            lines += text::wrap(labelFont, logical, relationMaxWidth);
    }
    return lines;
}

Link::LabelBox C4RelationLink::labelBox(const Board &board) const
{
    const auto &o = board.style().linkLabel;
    LabelBox box;
    box.lines = displayLines(board);
    box.lineHeight = o.fontPx + 2;
    box.width = box.lines.isEmpty() ? 0 : text::widest(Font(o.fontPx), box.lines) + o.padX * 2;
    box.height = box.lines.size() * (o.fontPx + 2) + o.padY * 2;
    return box;
}

QString C4RelationLink::layoutLabel(const Board &board) const
{
    return displayLines(board).join(QLatin1Char('\n'));
}

QString C4RelationLink::graphvizHtmlLabel(const Board &board) const
{
    // Graphviz reserves and positions the complete panel; the canvas redraws
    // it at the returned `lp`. No font here: the board declares for every
    // edge the font Graphviz has metrics for, already compensated.
    const Style &style = board.style();
    QStringList content;
    for (const QString &line : displayLines(board)) {
        if (!line.isEmpty())
            content.append(graphviz::dotHtmlEscape(line));
    }
    // CELLPADDING is a single value where the canvas has two: the horizontal
    // one, the larger. Reserving a little too much height is the safe
    // direction; too little lets routes cross the panel.
    return QStringLiteral("<TABLE BORDER=\"1\" CELLBORDER=\"0\" CELLSPACING=\"0\" CELLPADDING=\"%1\" "
                          "BGCOLOR=\"%2\" COLOR=\"%3\"><TR><TD ALIGN=\"CENTER\">%4</TD></TR></TABLE>")
        .arg(qRound(style.linkLabel.padX))
        .arg(hexColor(style.palette.labelBg), hexColor(style.palette.labelStroke),
             content.join(QStringLiteral("<BR/>")));
}

QStringList C4RelationLink::graphvizAttributes(const Board &) const
{
    if (bidirectional)
        return {QStringLiteral("dir=both"), QStringLiteral("arrowtail=normal")};
    return {};
}

void C4RelationLink::draw(QPainter &painter, const Board &board, bool selected) const
{
    const std::optional<Route> r = route(board);
    if (!r)
        return;
    const LinkStyle style = drawStyle(board, selected);
    const qreal inset = arrowBaseInset(style.arrow);
    strokeBody(painter, *r, {style.color, style.width, {}, inset, bidirectional ? inset : 0});
    fillArrowHead(painter, r->b, arrivalAngle(*r), style.arrow, style.color);
    // An exchange both ways: a second head at the tail, pointing back.
    if (bidirectional)
        fillArrowHead(painter, r->a, departureAngle(*r) + M_PI, style.arrow, style.color);
}

QList<Property> C4RelationLink::properties() const
{
    return {
        {QStringLiteral("technology"), tr("Technology"), Property::Kind::Text, {}},
        {QStringLiteral("description"), tr("Description"), Property::Kind::Text, {}},
        {QStringLiteral("bidirectional"), tr("Both ways"), Property::Kind::Bool, {}},
    };
}

QVariant C4RelationLink::propertyValue(const QString &id) const
{
    if (id == QLatin1String("technology"))
        return technology;
    if (id == QLatin1String("description"))
        return description;
    if (id == QLatin1String("bidirectional"))
        return bidirectional;
    return {};
}

void C4RelationLink::setPropertyValue(const QString &id, const QVariant &value)
{
    if (id == QLatin1String("technology"))
        technology = value.toString().trimmed();
    else if (id == QLatin1String("description"))
        description = value.toString();
    else if (id == QLatin1String("bidirectional"))
        bidirectional = value.toBool();
}

} // namespace diagram::c4

// ---------------------------------------------------------------------------
// The family

namespace diagram::families {

const Family &c4()
{
    using namespace diagram::c4;
    static const Family family = [] {
        Family f;
        f.id = QStringLiteral("c4");
        f.title = tr("C4 architecture");
        f.shapes = {{QStringLiteral("c4boundary"), [] { return std::unique_ptr<Shape>(new C4BoundaryShape); }},
                    {QStringLiteral("c4"), [] { return std::unique_ptr<Shape>(new C4ElementShape); }}};
        f.links = {
            {QStringLiteral("c4relation"), [] { return std::unique_ptr<Link>(new C4RelationLink); }, false, true}};
        f.defaultShape = QStringLiteral("c4");
        f.defaultLink = QStringLiteral("c4relation");
        f.graphvizClusters = true;
        f.graphvizSplines = QStringLiteral("spline");
        f.graphvizAdaptiveDirection = true;
        f.nestedCreation = true;

        // Elements: people, then each structural kind as a box, a database
        // and a queue, internal and external; then the boundaries.
        const auto element = [](const QString &kind, const QString &variant, bool external, const QString &label,
                                const QString &name, const QString &help) {
            QStringList id{kind};
            if (variant != QLatin1String("box"))
                id.append(variant);
            if (external)
                id.append(QStringLiteral("external"));
            return ElementPreset{id.join(QLatin1Char('-')), label, help, QStringLiteral("c4"),
                                 QJsonObject{{QStringLiteral("name"), name},
                                             {QStringLiteral("kind"), kind},
                                             {QStringLiteral("description"), QString()},
                                             {QStringLiteral("technology"), QString()},
                                             {QStringLiteral("external"), external},
                                             {QStringLiteral("variant"), variant}}};
        };
        f.elements = {
            element(QStringLiteral("person"), QStringLiteral("box"), false, tr("Person"), tr("Person"), tr("Person")),
            element(QStringLiteral("person"), QStringLiteral("box"), true, tr("External person"), tr("External person"),
                    tr("Person")),
        };
        struct Structural {
            const char *kind;
            QString noun;
            QString abbreviation;
        };
        const Structural structurals[] = {{"system", tr("System"), QStringLiteral("SYS")},
                                          {"container", tr("Container"), QStringLiteral("CTR")},
                                          {"component", tr("Component"), QStringLiteral("CMP")}};
        for (const Structural &s : structurals) {
            for (const char *variant : {"box", "database", "queue"}) {
                for (const bool external : {false, true}) {
                    const QString v = QString::fromLatin1(variant);
                    const QString base = v == QLatin1String("database") ? tr("Database")
                                         : v == QLatin1String("queue")  ? tr("Message queue")
                                                                        : s.noun;
                    const QString name = external ? tr("External %1").arg(base.toLower()) : base;
                    // The menu tells the kinds apart ("Container database");
                    // the element is named by what it is ("Database").
                    QString label = v == QLatin1String("box") ? s.noun : s.noun + QLatin1Char(' ') + base.toLower();
                    if (external)
                        label = tr("External %1").arg(label.toLower());
                    const QString help = s.abbreviation + (v == QLatin1String("database") ? QStringLiteral(" · DB")
                                                           : v == QLatin1String("queue")  ? QStringLiteral(" · QUEUE")
                                                                                          : QString());
                    f.elements.append(element(QString::fromLatin1(s.kind), v, external, label, name, help));
                }
            }
        }
        const auto boundary = [](const char *id, const QString &label, const char *kind) {
            return ElementPreset{QString::fromLatin1(id),
                                 label,
                                 {},
                                 QStringLiteral("c4boundary"),
                                 QJsonObject{{QStringLiteral("name"), label},
                                             {QStringLiteral("boundaryKind"), QString::fromLatin1(kind)}}};
        };
        f.elements += {boundary("enterprise-boundary", tr("Enterprise boundary"), "Enterprise_Boundary"),
                       boundary("system-boundary", tr("System boundary"), "System_Boundary"),
                       boundary("container-boundary", tr("Container boundary"), "Container_Boundary"),
                       boundary("deployment-boundary", tr("Deployment node"), "Deployment_Node")};

        const auto relation = [](const char *id, const QString &label, const QString &edgeLabel, bool both) {
            QJsonObject props{{QStringLiteral("label"), edgeLabel}};
            props.insert(both ? QStringLiteral("bidirectional") : QStringLiteral("directed"), true);
            return RelationPreset{QString::fromLatin1(id), label, {}, props};
        };
        f.relations = {relation("uses", tr("Uses"), tr("Uses"), false),
                       relation("calls", tr("Calls"), tr("Calls"), false),
                       relation("sends", tr("Sends"), tr("Sends"), false),
                       relation("reads", tr("Reads"), tr("Reads"), false),
                       relation("writes", tr("Writes"), tr("Writes"), false),
                       relation("publishes", tr("Publishes"), tr("Publishes"), false),
                       relation("consumes", tr("Consumes"), tr("Consumes"), false),
                       relation("hosts", tr("Hosts"), tr("Hosts"), false),
                       relation("depends", tr("Depends on"), tr("Depends on"), false),
                       relation("exchanges", tr("Bidirectional exchange"), tr("Exchanges with"), true)};
        f.technologies = {QStringLiteral("HTTP/HTTPS"), QStringLiteral("REST/JSON"),
                          QStringLiteral("WebSocket"),  QStringLiteral("gRPC"),
                          QStringLiteral("SQL"),        QStringLiteral("AMQP"),
                          QStringLiteral("Kafka"),      tr("Event")};
        return f;
    }();
    return family;
}

} // namespace diagram::families
