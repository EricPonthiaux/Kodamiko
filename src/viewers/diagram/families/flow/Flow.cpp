#include "Flow.h"

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/families/Families.h"

#include <QCoreApplication>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace diagram::flow {

namespace {

const Font font(14);
constexpr qreal lineHeight = 20;
constexpr qreal pad = 14;
constexpr qreal slant = 20;
constexpr qreal databaseCapHeight = 26;
constexpr qreal databaseContentGap = 8;
constexpr qreal subroutineBarInset = 14;
constexpr qreal specialShapeTextGap = 8;
constexpr qreal doubleCircleInset = 7;

QString tr(const char *text)
{
    return QCoreApplication::translate("FlowDiagram", text);
}

bool isOneOf(const QString &value, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        if (value == QLatin1String(name))
            return true;
    }
    return false;
}

const QList<std::pair<QString, QString>> &variantChoices()
{
    static const QList<std::pair<QString, QString>> list{
        {QStringLiteral("square"), tr("Process")},
        {QStringLiteral("round"), tr("Rounded process")},
        {QStringLiteral("stadium"), tr("Start / end")},
        {QStringLiteral("subroutine"), tr("Subprocess")},
        {QStringLiteral("cylinder"), tr("Database")},
        {QStringLiteral("circle"), tr("Start")},
        {QStringLiteral("odd"), tr("Asymmetric operation")},
        {QStringLiteral("diamond"), tr("Decision")},
        {QStringLiteral("hexagon"), tr("Preparation")},
        {QStringLiteral("leanr"), tr("Input / output")},
        {QStringLiteral("leanl"), tr("Output / input")},
        {QStringLiteral("trap"), tr("Priority action")},
        {QStringLiteral("invtrap"), tr("Manual operation")},
        {QStringLiteral("doublecircle"), tr("Stop")},
        {QStringLiteral("ellipse"), tr("Event")},
    };
    return list;
}

} // namespace

QString typeOf(const QString &variant)
{
    if (variant == QLatin1String("text"))
        return QStringLiteral("flowText");
    if (isOneOf(variant, {"diamond", "hexagon"}))
        return QStringLiteral("decision");
    if (isOneOf(variant, {"circle", "doublecircle", "stadium", "ellipse"}))
        return QStringLiteral("terminator");
    return QStringLiteral("process");
}

// ---------------------------------------------------------------------------
// FlowShape

FlowShape::FlowShape(const QString &type)
    : Shape(type, 150, 70)
{
    if (type == QLatin1String("decision")) {
        w = 170;
        h = 96;
    } else if (type == QLatin1String("terminator")) {
        w = 140;
        h = 62;
    }
}

void FlowShape::load(const QJsonObject &props)
{
    Shape::load(props);
    text = jsonString(props, QStringLiteral("text"));
    fill = jsonString(props, QStringLiteral("fill"), QStringLiteral("#ffffff"));
    variant = jsonString(props, QStringLiteral("variant"));
}

QJsonObject FlowShape::toJson() const
{
    QJsonObject json = Shape::toJson();
    json.insert(QStringLiteral("text"), text);
    json.insert(QStringLiteral("fill"), fill);
    if (!variant.isEmpty())
        json.insert(QStringLiteral("variant"), variant);
    return json;
}

qreal FlowShape::sizeFactor() const
{
    if (type == QLatin1String("decision"))
        return variant == QLatin1String("hexagon") ? 1.25 : 2;
    return 1;
}

qreal FlowShape::maxMeasureWidth() const
{
    // The factor 2 fits the text block in the diamond's central rectangle;
    // this bound keeps its historical outer width.
    if (type == QLatin1String("decision"))
        return variant == QLatin1String("hexagon") ? 260 : 195;
    return 260;
}

qreal FlowShape::textPaddingX() const
{
    if (variant == QLatin1String("subroutine"))
        return subroutineBarInset + specialShapeTextGap;
    if (isOneOf(variant, {"leanr", "leanl", "trap", "invtrap"}))
        return slant + specialShapeTextGap;
    if (variant == QLatin1String("doublecircle"))
        return doubleCircleInset + specialShapeTextGap;
    return pad;
}

qreal FlowShape::textEditPaddingX() const
{
    if (variant == QLatin1String("doublecircle") && textBoxWidth_)
        return (w - *textBoxWidth_) / 2;
    const qreal factor = sizeFactor();
    if (factor <= 1)
        return textPaddingX();
    const qreal safeWidth = w / factor - textPaddingX() * 2;
    return (w - safeWidth) / 2;
}

qreal FlowShape::labelCenterY() const
{
    if (variant != QLatin1String("cylinder"))
        return cy();
    const qreal count = std::max<qsizetype>(1, lines_.size());
    return y + databaseCapHeight + databaseContentGap + count * lineHeight / 2;
}

void FlowShape::measure(const Board &)
{
    // The shape follows its text: a bounded width (the text wraps beyond),
    // a height by line count; non-rectangular outlines reserve more area.
    const qreal factor = sizeFactor();
    const qreal maxWidth = maxMeasureWidth();
    const qreal padX = textPaddingX();
    const QStringList probe = text::wrap(font, text, maxWidth - padX * 2);
    const qreal widest = text::widest(font, probe);
    w = std::round(std::max<qreal>(120, std::min(maxWidth, widest + padX * 2 + 6)) * factor);
    h = std::round(std::max<qreal>(60, probe.size() * lineHeight + pad * 2) * factor);
    if (variant == QLatin1String("cylinder"))
        h = std::max(h, databaseCapHeight + databaseContentGap + probe.size() * lineHeight + pad);
    if (isOneOf(variant, {"circle", "doublecircle"})) {
        const qreal diameter = std::max({qreal(82), w, h});
        w = h = diameter;
    }
    lines_ = text::wrap(font, text, w / factor - padX * 2);
    textBoxWidth_.reset();
    if (type == QLatin1String("terminator") && variant == QLatin1String("doublecircle")) {
        textBoxWidth_ = std::max<qreal>(48, text::widest(font, lines_));
        const qreal safeWidth = *textBoxWidth_ + specialShapeTextGap * 2;
        const qreal safeHeight = lines_.size() * lineHeight + specialShapeTextGap * 2;
        const qreal required = std::ceil(std::hypot(safeWidth, safeHeight) + doubleCircleInset * 2);
        w = h = std::max({w, h, required});
    }
}

void FlowShape::resizeTo(qreal width, qreal height, const Board &board)
{
    if (isOneOf(variant, {"circle", "doublecircle"})) {
        const qreal size = std::max({width, height, resizeMinWidth, resizeMinHeight});
        Shape::resizeTo(size, size, board);
        w = h = std::max(w, h);
    } else {
        Shape::resizeTo(width, height, board);
    }
    const qreal available = std::max<qreal>(20, w / sizeFactor() - textPaddingX() * 2);
    lines_ = text::wrap(font, text, available);
}

void FlowShape::onDoubleClick(Board &board)
{
    TextEditSpec spec;
    spec.get = [this] { return text; };
    spec.set = [this](const QString &value) { text = value; };
    spec.color = board.style().palette.ink;
    spec.font = font;
    spec.lineHeight = lineHeight;
    spec.align = Align::Center;
    spec.rect = [this] {
        const qreal th = std::max<qsizetype>(1, lines_.size()) * lineHeight;
        const qreal padX = textEditPaddingX();
        return QRectF(x + padX, labelCenterY() - th / 2, w - padX * 2, th);
    };
    board.beginTextEdit(this, spec);
}

QList<QPointF> FlowShape::points() const
{
    const qreal r = x + w;
    const qreal b = y + h;
    if (type == QLatin1String("decision")) {
        if (variant == QLatin1String("hexagon")) {
            const qreal inset = std::min<qreal>(28, w * .2);
            return {{x + inset, y}, {r - inset, y}, {r, cy()}, {r - inset, b}, {x + inset, b}, {x, cy()}};
        }
        return {{cx(), y}, {r, cy()}, {cx(), b}, {x, cy()}};
    }
    const qreal s = std::min(slant, w * .18);
    if (variant == QLatin1String("leanr"))
        return {{x + s, y}, {r, y}, {r - s, b}, {x, b}};
    if (variant == QLatin1String("leanl"))
        return {{x, y}, {r - s, y}, {r, b}, {x + s, b}};
    if (variant == QLatin1String("trap"))
        return {{x + s, y}, {r - s, y}, {r, b}, {x, b}};
    if (variant == QLatin1String("invtrap"))
        return {{x, y}, {r, y}, {r - s, b}, {x + s, b}};
    if (variant == QLatin1String("odd"))
        return {{x, y}, {r - s, y}, {r, cy()}, {r - s, b}, {x, b}};
    return {{x, y}, {r, y}, {r, b}, {x, b}};
}

QPainterPath FlowShape::outline() const
{
    if (type == QLatin1String("terminator")) {
        if (variant == QLatin1String("stadium"))
            return roundRect(x, y, w, h, h / 2);
        return ellipse(QPointF(cx(), cy()), w / 2, h / 2);
    }
    if (type == QLatin1String("decision"))
        return polygon(points());
    if (variant == QLatin1String("round"))
        return roundRect(x, y, w, h, std::min<qreal>(18, h / 3));
    if (variant == QLatin1String("cylinder")) {
        const qreal cap = databaseCapHeight;
        QPainterPath path;
        path.moveTo(x, y + cap / 2);
        path.arcTo(QRectF(x, y, w, cap), 180, -180);
        path.lineTo(x + w, y + h - cap / 2);
        path.arcTo(QRectF(x, y + h - cap, w, cap), 0, -180);
        path.closeSubpath();
        return path;
    }
    if (variant.isEmpty() || isOneOf(variant, {"square", "subroutine"}))
        return roundRect(x, y, w, h, shapeRadiusSm);
    return polygon(points());
}

void FlowShape::drawDetails(QPainter &painter, const QColor &color, qreal width) const
{
    if (variant == QLatin1String("subroutine")) {
        const qreal inset = std::min(subroutineBarInset, w * .1);
        QPainterPath bars;
        bars.moveTo(x + inset, y);
        bars.lineTo(x + inset, y + h);
        bars.moveTo(x + w - inset, y);
        bars.lineTo(x + w - inset, y + h);
        stroke(painter, bars, color, width);
    } else if (variant == QLatin1String("cylinder")) {
        stroke(painter, ellipse(QPointF(cx(), y + databaseCapHeight / 2), w / 2, databaseCapHeight / 2), color, width);
    } else if (variant == QLatin1String("doublecircle")) {
        const qreal inset = std::min(doubleCircleInset, w * .08);
        stroke(painter, ellipse(QPointF(cx(), cy()), w / 2 - inset, h / 2 - inset), color, width);
    }
}

QList<Port> FlowShape::ports(const Board &) const
{
    QList<Port> result;
    for (const QPointF &toward :
         {QPointF(cx(), y - h), QPointF(x + w * 2, cy()), QPointF(cx(), y + h * 2), QPointF(x - w, cy())}) {
        const QPointF p = borderPoint(toward);
        result.append({p.x(), p.y(), {}});
    }
    return result;
}

QPointF FlowShape::borderPoint(QPointF toward) const
{
    const QPointF center(cx(), cy());
    if (type == QLatin1String("terminator")) {
        const qreal dx = toward.x() - cx();
        const qreal dy = toward.y() - cy();
        if (!dx && !dy)
            return center;
        if (variant == QLatin1String("stadium") && w > h) {
            const qreal radius = h / 2;
            const qreal straight = w / 2 - radius;
            const qreal verticalScale = dy ? radius / std::abs(dy) : std::numeric_limits<qreal>::infinity();
            if (std::abs(dx * verticalScale) <= straight)
                return {cx() + dx * verticalScale, cy() + dy * verticalScale};
            const qreal capX = cx() + (dx >= 0 ? 1 : -1) * straight;
            const qreal ox = cx() - capX;
            const qreal a = dx * dx + dy * dy;
            const qreal b = 2 * ox * dx;
            const qreal c = ox * ox - radius * radius;
            const qreal discriminant = std::max<qreal>(0, b * b - 4 * a * c);
            const qreal t =
                std::max((-b - std::sqrt(discriminant)) / (2 * a), (-b + std::sqrt(discriminant)) / (2 * a));
            return {cx() + dx * t, cy() + dy * t};
        }
        return ellipseBorderPoint(center, toward, w / 2, h / 2);
    }
    if (type == QLatin1String("process") && isOneOf(variant, {"cylinder", "round"}))
        return Shape::borderPoint(toward);
    return polygonBorderPoint(center, toward, points());
}

QString FlowShape::graphvizGeometry() const
{
    if (type == QLatin1String("decision"))
        return variant == QLatin1String("hexagon") ? QStringLiteral("hexagon") : QStringLiteral("diamond");
    // The board's outlines in Graphviz's vocabulary, so that it routes
    // against the outline drawn. `leanl` has none: Graphviz cannot mirror its
    // parallelogram.
    static const QHash<QString, QString> geometry{
        {QStringLiteral("circle"), QStringLiteral("ellipse")},
        {QStringLiteral("doublecircle"), QStringLiteral("ellipse")},
        {QStringLiteral("hexagon"), QStringLiteral("hexagon")},
        {QStringLiteral("diamond"), QStringLiteral("diamond")},
        {QStringLiteral("trap"), QStringLiteral("trapezium")},
        {QStringLiteral("invtrap"), QStringLiteral("invtrapezium")},
        {QStringLiteral("leanr"), QStringLiteral("parallelogram")},
    };
    return geometry.value(variant, QStringLiteral("box"));
}

QStringList FlowShape::graphvizNodeAttributes(const Board &, const QString &, qreal width, qreal height) const
{
    return {QStringLiteral("shape=") + graphvizGeometry(),
            QStringLiteral("fixedsize=true"),
            QStringLiteral("margin=0"),
            QStringLiteral("label=\"\""),
            QStringLiteral("width=") + QString::number(std::max<qreal>(1, width) / 72, 'f', 3),
            QStringLiteral("height=") + QString::number(std::max<qreal>(1, height) / 72, 'f', 3)};
}

void FlowShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QColor defaultFill = type == QLatin1String("decision")     ? style.tones.warm
                               : type == QLatin1String("terminator") ? style.tones.cool
                                                                     : style.tones.sage;
    const QColor own = parseColor(fill);
    const QColor nativeFill = !own.isValid() || own == QColor(Qt::white) ? defaultFill : own;
    const QPainterPath path = outline();
    shadowFill(painter, path, fillColor(nativeFill), style);
    const QColor line = strokeColor(state.selected ? style.shape.selectedStroke : style.palette.border);
    const qreal width = state.selected ? 2 : shapeBorderWidth;
    stroke(painter, path, line, width);
    drawDetails(painter, line, width);
    if (state.editing)
        return;
    painter.save();
    painter.setClipPath(path, Qt::IntersectClip);
    const QColor ink = textColor(style.palette.ink);
    qreal ty = labelCenterY() - ((lines_.size() - 1) * lineHeight) / 2;
    for (const QString &line : lines_) {
        text::draw(painter, font, line, QPointF(cx(), ty), ink, Align::Center, Baseline::Middle);
        ty += lineHeight;
    }
    painter.restore();
}

QList<Property> FlowShape::properties() const
{
    return {{QStringLiteral("variant"), tr("Shape"), Property::Kind::Choice, variantChoices()}};
}

QVariant FlowShape::propertyValue(const QString &id) const
{
    if (id == QLatin1String("variant"))
        return variant.isEmpty() ? (type == QLatin1String("decision")     ? QStringLiteral("diamond")
                                    : type == QLatin1String("terminator") ? QStringLiteral("stadium")
                                                                          : QStringLiteral("square"))
                                 : variant;
    return {};
}

void FlowShape::setPropertyValue(const QString &id, const QVariant &value)
{
    if (id != QLatin1String("variant"))
        return;
    variant = value.toString();
    // The drawn type follows the variant: one class draws them all.
    type = typeOf(variant);
    manualWidth.reset();
    manualHeight.reset();
}

// ---------------------------------------------------------------------------
// FlowTextShape

FlowTextShape::FlowTextShape()
    : Shape(QStringLiteral("flowText"), 180, 60)
{
}

void FlowTextShape::load(const QJsonObject &props)
{
    Shape::load(props);
    text = jsonString(props, QStringLiteral("text"), jsonString(props, QStringLiteral("label")));
}

QJsonObject FlowTextShape::toJson() const
{
    QJsonObject json = Shape::toJson();
    json.insert(QStringLiteral("text"), text);
    json.insert(QStringLiteral("variant"), QStringLiteral("text"));
    return json;
}

QRectF FlowTextShape::textRect() const
{
    return {x + pad, y + pad, std::max<qreal>(20, w - pad * 2), std::max(lineHeight, h - pad * 2)};
}

void FlowTextShape::measure(const Board &)
{
    const QStringList lines = text::wrap(font, text, 280);
    const qreal safeWidth = std::max<qreal>(28, text::widest(font, lines) + pad * 2);
    const qreal safeHeight = std::max(lineHeight + pad * 2, lines.size() * lineHeight + pad * 2);
    minWidth_ = std::ceil(std::max<qreal>(60, safeWidth));
    minHeight_ = std::ceil(std::max<qreal>(34, safeHeight));
    w = std::max(w, minWidth_);
    h = std::max(h, minHeight_);
    lines_ = text::wrap(font, text, std::max<qreal>(20, textRect().width()));
}

void FlowTextShape::resizeTo(qreal width, qreal height, const Board &)
{
    w = std::max(width, minWidth_);
    h = std::max(height, minHeight_);
    if (!applyingMeasuredSize) {
        manualWidth = w;
        manualHeight = h;
    }
    lines_ = text::wrap(font, text, std::max<qreal>(20, textRect().width()));
}

void FlowTextShape::onDoubleClick(Board &board)
{
    TextEditSpec spec;
    spec.get = [this] { return text; };
    spec.set = [this](const QString &value) { text = value; };
    spec.color = board.style().palette.ink;
    spec.font = font;
    spec.lineHeight = lineHeight;
    spec.align = Align::Center;
    spec.rect = [this] { return textRect(); };
    board.beginTextEdit(this, spec);
}

void FlowTextShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    if (state.editing)
        return;
    const QColor ink = textColor(board.style().palette.ink);
    const QStringList lines = lines_.isEmpty() ? QStringList{QString()} : lines_;
    qreal ty = cy() - ((lines.size() - 1) * lineHeight) / 2;
    for (const QString &line : lines) {
        text::draw(painter, font, line, QPointF(cx(), ty), ink, Align::Center, Baseline::Middle);
        ty += lineHeight;
    }
    if (state.selected) {
        const QPen pen(withAlpha(board.style().shape.selectedStroke, 0.5), 1, Qt::DashLine);
        painter.save();
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect());
        painter.restore();
    }
}

// ---------------------------------------------------------------------------
// FlowLink

FlowLink::FlowLink()
    : Link(QStringLiteral("flow"))
{
}

void FlowLink::load(const QJsonObject &props)
{
    Link::load(props);
    line = jsonString(props, QStringLiteral("line"), QStringLiteral("solid"));
    head = jsonString(props, QStringLiteral("head"), QStringLiteral("arrow"));
}

QJsonObject FlowLink::toJson() const
{
    QJsonObject json = Link::toJson();
    if (line != QLatin1String("solid"))
        json.insert(QStringLiteral("line"), line);
    if (head != QLatin1String("arrow"))
        json.insert(QStringLiteral("head"), head);
    return json;
}

QStringList FlowLink::graphvizAttributes(const Board &board) const
{
    QStringList attributes;
    if (line == QLatin1String("thick"))
        attributes.append(QStringLiteral("penwidth=") + QString::number(board.style().link.width + 1.5, 'f', 3));
    if (line == QLatin1String("invisible"))
        attributes.append(QStringLiteral("style=invis"));
    if (head != QLatin1String("arrow"))
        attributes.append(QStringLiteral("arrowhead=none"));
    return attributes;
}

LinkStyle FlowLink::drawStyle(const Board &board, bool selected) const
{
    const qreal native = board.style().link.width;
    return Link::drawStyle(board, selected, line == QLatin1String("thick") ? native + 1.5 : native);
}

void FlowLink::draw(QPainter &painter, const Board &board, bool selected) const
{
    if ((line == QLatin1String("invisible")) && (!selected))
        // Invisible in the diagram; a faint dash keeps it findable when
        // selected.
        return;

    const std::optional<Route> r = route(board);
    if (!r)
        return;
    const LinkStyle style = drawStyle(board, selected);
    const QPointF b = r->b;
    const qreal angle = arrivalAngle(*r);
    const qreal headLength = head == QLatin1String("arrow") ? arrowBaseInset(style.arrow) : 0;
    QList<qreal> dash;
    if (line == QLatin1String("dotted"))
        dash = {4, 4};
    if (line == QLatin1String("invisible"))
        dash = {2, 6};
    strokeBody(painter, *r, {style.color, style.width, dash, headLength, 0});
    if (head == QLatin1String("arrow")) {
        fillArrowHead(painter, b, angle, style.arrow, style.color);
    } else if (head == QLatin1String("circle")) {
        const qreal radius = 4.5 * style.markerScale;
        const QPointF c(b.x() - 4 * style.markerScale * std::cos(angle),
                        b.y() - 4 * style.markerScale * std::sin(angle));
        const QPainterPath dot = ellipse(c, radius, radius);
        painter.fillPath(dot, board.style().background);
        stroke(painter, dot, style.color, style.width);
    } else if (head == QLatin1String("cross")) {
        const qreal s = 5 * style.markerScale;
        const QPointF c(b.x() - s * std::cos(angle), b.y() - s * std::sin(angle));
        QPainterPath cross;
        cross.moveTo(c.x() - s, c.y() - s);
        cross.lineTo(c.x() + s, c.y() + s);
        cross.moveTo(c.x() - s, c.y() + s);
        cross.lineTo(c.x() + s, c.y() - s);
        stroke(painter, cross, style.color, style.width);
    }
}

QList<Property> FlowLink::properties() const
{
    return {
        {QStringLiteral("line"),
         tr("Line"),
         Property::Kind::Choice,
         {{QStringLiteral("solid"), tr("Solid")},
          {QStringLiteral("thick"), tr("Thick")},
          {QStringLiteral("dotted"), tr("Dotted")},
          {QStringLiteral("invisible"), tr("Invisible")}}},
        {QStringLiteral("head"),
         tr("Head"),
         Property::Kind::Choice,
         {{QStringLiteral("arrow"), tr("Arrow")},
          {QStringLiteral("none"), tr("None")},
          {QStringLiteral("circle"), tr("Circle")},
          {QStringLiteral("cross"), tr("Cross")}}},
    };
}

QVariant FlowLink::propertyValue(const QString &id) const
{
    if (id == QLatin1String("line"))
        return line;
    if (id == QLatin1String("head"))
        return head;
    return {};
}

void FlowLink::setPropertyValue(const QString &id, const QVariant &value)
{
    if (id == QLatin1String("line"))
        line = value.toString();
    else if (id == QLatin1String("head"))
        head = value.toString();
}

} // namespace diagram::flow

// ---------------------------------------------------------------------------
// The family

namespace diagram::families {

const Family &flowchart()
{
    using namespace flow;
    static const Family family = [] {
        Family f;
        f.id = QStringLiteral("flowchart");
        f.title = tr("Flowchart");
        const auto shape = [](const char *type) {
            return ShapeType{QString::fromLatin1(type),
                             [type] { return std::unique_ptr<Shape>(new FlowShape(QString::fromLatin1(type))); }};
        };
        f.shapes = {shape("process"),
                    shape("decision"),
                    shape("terminator"),
                    {QStringLiteral("flowText"), [] { return std::unique_ptr<Shape>(new FlowTextShape); }}};
        f.links = {{QStringLiteral("flow"), [] { return std::unique_ptr<Link>(new FlowLink); }}};
        f.defaultShape = QStringLiteral("process");
        f.defaultLink = QStringLiteral("flow");
        f.hasDirection = true;
        const auto preset = [](const char *id, const QString &label, const QString &text, const char *variant) {
            const QString v = QString::fromLatin1(variant);
            return ElementPreset{QString::fromLatin1(id),
                                 label,
                                 {},
                                 typeOf(v),
                                 QJsonObject{{QStringLiteral("text"), text}, {QStringLiteral("variant"), v}}};
        };
        f.elements = {
            preset("flow-process", tr("Process"), tr("New step"), "square"),
            preset("flow-rounded", tr("Rounded process"), tr("New step"), "round"),
            preset("flow-terminator", tr("Start / end"), tr("Start / end"), "stadium"),
            preset("flow-subprocess", tr("Subprocess"), tr("Subprocess"), "subroutine"),
            preset("flow-database", tr("Database"), tr("Database"), "cylinder"),
            preset("flow-start", tr("Start"), tr("Start"), "circle"),
            preset("flow-asymmetric", tr("Asymmetric operation"), tr("Operation"), "odd"),
            preset("flow-decision", tr("Decision"), tr("Condition?"), "diamond"),
            preset("flow-preparation", tr("Preparation"), tr("Prepare"), "hexagon"),
            preset("flow-input-output", tr("Input / output"), tr("Data"), "leanr"),
            preset("flow-output-input", tr("Output / input"), tr("Data"), "leanl"),
            preset("flow-priority", tr("Priority action"), tr("Priority action"), "trap"),
            preset("flow-manual-operation", tr("Manual operation"), tr("Manual operation"), "invtrap"),
            preset("flow-stop", tr("Stop"), tr("Stop"), "doublecircle"),
            preset("flow-event", tr("Event"), tr("Event"), "ellipse"),
            preset("flow-text", tr("Free text"), tr("Free text"), "text"),
        };
        return f;
    }();
    return family;
}

} // namespace diagram::families
