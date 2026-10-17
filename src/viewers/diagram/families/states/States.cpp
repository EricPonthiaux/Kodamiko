#include "States.h"

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/families/Families.h"

#include <QCoreApplication>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace diagram::states {

namespace {

const Font font(13);
constexpr qreal titleFontPx = 14;
const Font titleFont(titleFontPx, 600);
constexpr qreal lineHeight = 20;
constexpr qreal pad = 14;
// A composite's title band, above its children.
constexpr qreal containerTitleHeight = 38;
constexpr qreal containerMinWidth = 300;
constexpr qreal containerMinHeight = 210;

QString tr(const char *text)
{
    return QCoreApplication::translate("StateDiagram", text);
}

QString defaultContainerText()
{
    return tr("Composite state");
}

QStringList wrapAll(const Font &f, const QStringList &texts, qreal width)
{
    QStringList lines;
    for (const QString &t : texts)
        lines += text::wrap(f, t, width);
    return lines;
}

QPainterPath circle(QPointF center, qreal radius)
{
    return ellipse(center, radius, radius);
}

} // namespace

// ---------------------------------------------------------------------------
// StateElement

StateElement::StateElement(const QString &type_, qreal width, qreal height)
    : Shape(type_, width, height)
{
}

void StateElement::load(const QJsonObject &props)
{
    Shape::load(props);
    sourceKey = jsonString(props, QStringLiteral("sourceKey"));
}

QJsonObject StateElement::toJson() const
{
    QJsonObject json = Shape::toJson();
    if (!sourceKey.isEmpty())
        json.insert(QStringLiteral("sourceKey"), sourceKey);
    return json;
}

// ---------------------------------------------------------------------------
// StateShape

StateShape::StateShape()
    : StateElement(QStringLiteral("state"), 150, 60)
{
}

void StateShape::load(const QJsonObject &props)
{
    StateElement::load(props);
    text = jsonString(props, QStringLiteral("text"));
    desc = jsonStrings(props, QStringLiteral("desc"));
    fill = jsonString(props, QStringLiteral("fill"), QStringLiteral("#ffffff"));
    if (fill.isEmpty())
        fill = QStringLiteral("#ffffff");
}

QJsonObject StateShape::toJson() const
{
    QJsonObject json = StateElement::toJson();
    json.insert(QStringLiteral("text"), text);
    json.insert(QStringLiteral("fill"), fill);
    if (!desc.isEmpty())
        json.insert(QStringLiteral("desc"), toJsonArray(desc));
    return json;
}

void StateShape::wrapLines()
{
    lines_ = wrapAll(font, desc, std::max<qreal>(20, w - pad * 2));
    titleLines_ = text::wrap(titleFont, text, std::max<qreal>(20, w - pad * 2));
}

void StateShape::measure(const Board &)
{
    // The width follows the widest of the name and the descriptions, up to a
    // bound past which they wrap; the height follows the lines.
    constexpr qreal maxWidth = 360;
    const qreal widest = std::max(text::widest(titleFont, text::wrap(titleFont, text, maxWidth - pad * 2)),
                                  text::widest(font, wrapAll(font, desc, maxWidth - pad * 2)));
    w = std::ceil(std::max<qreal>(150, std::min(maxWidth, widest + pad * 2)));
    lines_ = wrapAll(font, desc, w - pad * 2);
    titleLines_ = text::wrap(titleFont, text, w - pad * 2);
    const qsizetype titleCount = std::max<qsizetype>(1, titleLines_.size());
    const qreal body = lines_.isEmpty() ? 0 : lines_.size() * lineHeight + pad;
    h = std::max<qreal>(48, titleCount * lineHeight + pad * 2 + body);
}

void StateShape::resizeTo(qreal width, qreal height, const Board &board)
{
    StateElement::resizeTo(width, height, board);
    wrapLines();
}

void StateShape::onDoubleClick(Board &board)
{
    // First line: the state's name; the following ones: its descriptions.
    TextEditSpec spec;
    spec.get = [this] { return (QStringList{text} + desc).join(QLatin1Char('\n')); };
    spec.set = [this](const QString &value) {
        QStringList lines = value.split(QLatin1Char('\n'));
        text = lines.isEmpty() ? QString() : lines.takeFirst().trimmed();
        desc.clear();
        for (const QString &line : lines) {
            if (!line.trimmed().isEmpty())
                desc.append(line.trimmed());
        }
    };
    spec.color = board.style().palette.ink;
    spec.font = font;
    spec.lineHeight = lineHeight;
    spec.enterCommits = false;
    spec.rect = [this] { return QRectF(x + pad, y + pad, w - pad * 2, h - pad * 2); };
    board.beginTextEdit(this, spec);
}

void StateShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QColor own = parseColor(fill);
    const QColor nativeFill = !own.isValid() || own == QColor(Qt::white) ? style.tones.sage : own;
    const QPainterPath path = roundRect(x, y, w, h, ShapeRadiusSm);
    shadowFill(painter, path, fillColor(nativeFill), style);
    stroke(painter, path, strokeColor(state.selected ? style.shape.selectedStroke : style.palette.border),
           state.selected ? 2 : ShapeBorderWidth);
    if (state.editing)
        return;
    painter.save();
    painter.setClipPath(path, Qt::IntersectClip);
    const QColor ink = textColor(style.palette.ink);
    // Without a description the name floats dead center; otherwise it heads
    // the box.
    qreal ty = !lines_.isEmpty() ? y + pad + lineHeight / 2 : cy() - ((titleLines_.size() - 1) * lineHeight) / 2;
    for (const QString &line : titleLines_) {
        text::draw(painter, titleFont, line, QPointF(cx(), ty), ink, Align::Center, Baseline::Middle);
        ty += lineHeight;
    }
    ty -= lineHeight / 2;
    if (!lines_.isEmpty()) {
        ty += pad / 2;
        strokeLine(painter, QPointF(x, ty - pad / 4), QPointF(x + w, ty - pad / 4), style.palette.separator, 1);
        ty += pad / 2;
        const QColor muted = textColor(style.palette.inkMuted);
        for (const QString &line : lines_) {
            text::draw(painter, font, line, QPointF(x + pad, ty), muted, Align::Left, Baseline::Top);
            ty += lineHeight;
        }
    }
    painter.restore();
}

QList<Property> StateShape::properties() const
{
    return {{QStringLiteral("desc"), tr("Description"), Property::Kind::Lines}};
}

QVariant StateShape::propertyValue(const QString &id) const
{
    if (id == QLatin1String("desc"))
        return desc.join(QLatin1Char('\n'));
    return {};
}

void StateShape::setPropertyValue(const QString &id, const QVariant &value)
{
    if (id != QLatin1String("desc"))
        return;
    desc.clear();
    for (const QString &line : value.toString().split(QLatin1Char('\n'))) {
        if (!line.trimmed().isEmpty())
            desc.append(line.trimmed());
    }
    manualWidth.reset();
    manualHeight.reset();
}

// ---------------------------------------------------------------------------
// StateContainerShape

StateContainerShape::StateContainerShape()
    : StateElement(QStringLiteral("stateContainer"), 340, 230)
{
    text = defaultContainerText();
}

void StateContainerShape::load(const QJsonObject &props)
{
    StateElement::load(props);
    text = props.contains(QStringLiteral("text")) ? jsonString(props, QStringLiteral("text")) : defaultContainerText();
}

QJsonObject StateContainerShape::toJson() const
{
    QJsonObject json = StateElement::toJson();
    json.insert(QStringLiteral("text"), text);
    return json;
}

void StateContainerShape::measure(const Board &board)
{
    // An empty composite keeps sidecar's floor, room to drop children in.
    // One that holds children is a Graphviz cluster sized on them: a floor
    // imposed after the layout would push it into its neighbors, or out of
    // its own parent.
    labelWidth_ = text::width(titleFont, text) + pad * 2;
    if (board.childShapes(this).isEmpty())
        minimum_ = QSizeF(std::max(containerMinWidth, labelWidth_ + pad * 2), containerMinHeight);
    else
        minimum_ = QSizeF(labelWidth_ + pad * 2, containerTitleHeight + 40);
    // Its size is the layout's or the user's, never derived from a text: it
    // is kept as chosen, so that the file keeps it too. A size set since the
    // last measure (by the layout) is the one chosen; otherwise the chosen
    // one stands (restored from the file, or resized by hand).
    if (!measured_ || *measured_ != QSizeF(w, h) || !manualWidth || !manualHeight) {
        manualWidth = w;
        manualHeight = h;
    }
    w = std::max(minimum_.width(), *manualWidth);
    h = std::max(minimum_.height(), *manualHeight);
    measured_ = QSizeF(w, h);
}

std::optional<QSizeF> StateContainerShape::resizeMinimum(const Board &) const
{
    return minimum_;
}

void StateContainerShape::resizeTo(qreal width, qreal height, const Board &board)
{
    // The measured size is the one chosen: its children are where the layout
    // put them, or not placed yet while a file is read. A resize by hand
    // still never passes under them.
    if (applyingMeasuredSize) {
        w = std::max(width, resizeMinWidth);
        h = std::max(height, resizeMinHeight);
        return;
    }
    StateElement::resizeTo(width, height, board);
}

bool StateContainerShape::hitTest(QPointF p) const
{
    constexpr qreal tolerance = 9;
    if (!Shape::hitTest(p))
        return false;
    return p.y() <= y + containerTitleHeight || p.x() - x <= tolerance || x + w - p.x() <= tolerance
           || y + h - p.y() <= tolerance;
}

std::optional<QRectF> StateContainerShape::childBounds(const Board &) const
{
    // Below the title band, off the border (as the free board's frames).
    return QRectF(QPointF(x + 12, y + containerTitleHeight + 8), QPointF(x + w - 12, y + h - 12));
}

qreal StateContainerShape::layoutLabelFontPx() const
{
    // The title really painted: the band Graphviz must reserve on top of the
    // cluster.
    return titleFontPx;
}

std::optional<QSizeF> StateContainerShape::layoutMinimumSize() const
{
    // The floor measure() will take back, whatever Graphviz deduced from the
    // content alone.
    return minimum_;
}

void StateContainerShape::onDoubleClick(Board &board)
{
    TextEditSpec spec;
    spec.get = [this] { return text; };
    spec.set = [this](const QString &value) {
        text = value.trimmed().isEmpty() ? defaultContainerText() : value.trimmed();
    };
    spec.color = board.style().palette.ink;
    spec.font = font;
    spec.lineHeight = lineHeight;
    spec.rect = [this] { return QRectF(x + pad, y + 9, w - pad * 2, 22); };
    board.beginTextEdit(this, spec);
}

void StateContainerShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QPainterPath path = roundRect(x, y, w, h, ShapeRadiusSm);
    painter.save();
    painter.setOpacity(painter.opacity() * (customFill.isEmpty() ? 0.72 : 0.55));
    shadowFill(painter, path, fillColor(style.tones.group.isValid() ? style.tones.group : style.tones.sage), style);
    painter.restore();
    stroke(painter, path, strokeColor(state.selected ? style.shape.selectedStroke : style.palette.border),
           state.selected ? 2 : ShapeBorderWidth);
    strokeLine(painter, QPointF(x, y + containerTitleHeight), QPointF(x + w, y + containerTitleHeight),
               style.palette.separator, 1);
    if (state.editing)
        return;
    text::draw(painter, titleFont, text, QPointF(x + pad, y + containerTitleHeight / 2), textColor(style.palette.ink),
               Align::Left, Baseline::Middle);
}

// ---------------------------------------------------------------------------
// RoundShape

RoundShape::RoundShape(const QString &type_, qreal size)
    : StateElement(type_, size, size)
{
}

bool RoundShape::hitTest(QPointF p) const
{
    const qreal dx = p.x() - cx(), dy = p.y() - cy();
    return dx * dx + dy * dy <= (w / 2) * (w / 2);
}

QPointF RoundShape::borderPoint(QPointF toward) const
{
    const qreal dx = toward.x() - cx(), dy = toward.y() - cy();
    if (!dx && !dy)
        return {cx(), cy()};
    const qreal t = (w / 2) / std::hypot(dx, dy);
    return {cx() + dx * t, cy() + dy * t};
}

QString RoundShape::displayText() const
{
    return type == QLatin1String("initial") ? tr("Initial state") : tr("Final state");
}

void RoundShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QPointF center(cx(), cy());
    const QPainterPath outer = circle(center, w / 2);
    if (type == QLatin1String("initial")) {
        shadowFill(painter, outer, fillColor(state.selected ? style.shape.selectedStroke : style.palette.solid), style);
        if (!customFill.isEmpty())
            stroke(painter, outer, strokeColor(style.palette.solid), ShapeBorderWidth);
        return;
    }
    // The final state: a ring around a bullet.
    const QColor color = fillColor(state.selected ? style.shape.selectedStroke : style.palette.solid);
    shadowFill(painter, outer, style.background, style);
    stroke(painter, outer, strokeColor(color), 1.5);
    painter.fillPath(circle(center, w / 2 - 4), color);
}

// ---------------------------------------------------------------------------
// ChoiceShape

ChoiceShape::ChoiceShape()
    : StateElement(QStringLiteral("choice"), 36, 36)
{
}

void ChoiceShape::load(const QJsonObject &props)
{
    StateElement::load(props);
    text = jsonString(props, QStringLiteral("text"));
}

QJsonObject ChoiceShape::toJson() const
{
    QJsonObject json = StateElement::toJson();
    json.insert(QStringLiteral("text"), text);
    return json;
}

void ChoiceShape::measure(const Board &)
{
    if (text.trimmed().isEmpty()) {
        w = h = 36;
        lines_.clear();
        return;
    }
    const qreal widest = text::widest(font, text::wrap(font, text, 140));
    // A diamond narrows toward its vertices: the extra room keeps every line
    // inside its central readable area.
    w = std::ceil(std::max<qreal>(96, std::min<qreal>(210, widest + 52)));
    lines_ = text::wrap(font, text, w - 52);
    h = std::ceil(std::max<qreal>(72, lines_.size() * lineHeight + 42));
}

void ChoiceShape::resizeTo(qreal width, qreal height, const Board &board)
{
    StateElement::resizeTo(width, height, board);
    lines_ = text.trimmed().isEmpty() ? QStringList() : text::wrap(font, text, std::max<qreal>(20, w - 52));
}

void ChoiceShape::onDoubleClick(Board &board)
{
    TextEditSpec spec;
    spec.get = [this] { return text; };
    spec.set = [this](const QString &value) { text = value.trimmed(); };
    spec.color = board.style().palette.ink;
    spec.font = font;
    spec.lineHeight = lineHeight;
    spec.align = Align::Center;
    spec.rect = [this] { return QRectF(x + w * 0.22, y + h * 0.24, w * 0.56, h * 0.52); };
    board.beginTextEdit(this, spec);
}

QPointF ChoiceShape::borderPoint(QPointF toward) const
{
    const qreal dx = toward.x() - cx(), dy = toward.y() - cy();
    if (!dx && !dy)
        return {cx(), cy()};
    const qreal t = 1 / (std::abs(dx) / (w / 2) + std::abs(dy) / (h / 2));
    return {cx() + dx * t, cy() + dy * t};
}

QStringList ChoiceShape::graphvizNodeAttributes(const Board &, const QString &, qreal width, qreal height) const
{
    // Graphviz routes against the visible diamond, not the measurement box
    // around it: otherwise transitions stop short of the outline.
    return {QStringLiteral("shape=diamond"),
            QStringLiteral("fixedsize=true"),
            QStringLiteral("margin=0"),
            QStringLiteral("label=\"\""),
            QStringLiteral("width=") + QString::number(std::max<qreal>(1, width) / 72, 'f', 3),
            QStringLiteral("height=") + QString::number(std::max<qreal>(1, height) / 72, 'f', 3)};
}

QPainterPath ChoiceShape::outline() const
{
    return polygon({{cx(), y}, {x + w, cy()}, {cx(), y + h}, {x, cy()}});
}

void ChoiceShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QPainterPath path = outline();
    shadowFill(painter, path, fillColor(style.tones.warm), style);
    stroke(painter, path, strokeColor(state.selected ? style.shape.selectedStroke : style.palette.border),
           state.selected ? 2 : ShapeBorderWidth);
    if (state.editing || text.isEmpty())
        return;
    painter.save();
    painter.setClipPath(path, Qt::IntersectClip);
    const QColor ink = textColor(style.palette.ink);
    const QStringList lines = lines_.isEmpty() ? QStringList{text} : lines_;
    qreal ty = cy() - ((lines.size() - 1) * lineHeight) / 2;
    for (const QString &line : lines) {
        text::draw(painter, font, line, QPointF(cx(), ty), ink, Align::Center, Baseline::Middle);
        ty += lineHeight;
    }
    painter.restore();
}

// ---------------------------------------------------------------------------
// ForkJoinShape

ForkJoinShape::ForkJoinShape(const QString &type_)
    : StateElement(type_, 100, 12)
{
}

QString ForkJoinShape::displayText() const
{
    return type == QLatin1String("fork") ? tr("Fork") : tr("Join");
}

void ForkJoinShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QPainterPath path = roundRect(x, y, w, h, ShapeRadiusSm);
    shadowFill(painter, path, fillColor(state.selected ? style.shape.selectedStroke : style.palette.solid), style);
    if (!customFill.isEmpty())
        stroke(painter, path, strokeColor(style.palette.solid), ShapeBorderWidth);
}

QList<Property> ForkJoinShape::properties() const
{
    return {{QStringLiteral("kind"),
             tr("Kind"),
             Property::Kind::Choice,
             {{QStringLiteral("fork"), tr("Fork")}, {QStringLiteral("join"), tr("Join")}}}};
}

QVariant ForkJoinShape::propertyValue(const QString &id) const
{
    return id == QLatin1String("kind") ? QVariant(type) : QVariant();
}

void ForkJoinShape::setPropertyValue(const QString &id, const QVariant &value)
{
    // Same bar, other meaning: the type follows the choice.
    if (id == QLatin1String("kind") && (value == QLatin1String("fork") || value == QLatin1String("join")))
        type = value.toString();
}

// ---------------------------------------------------------------------------
// TransitionLink

namespace {

qreal labelHeight(const Board &board, const Link &link)
{
    if (link.label.isEmpty())
        return 24;
    const auto &o = board.style().linkLabel;
    return link.label.split(QLatin1Char('\n')).size() * (o.fontPx + 2) + o.padY * 2;
}

// The ends of a self-loop and its quadratic's control point, beyond the
// shape's top-right corner.
struct Loop {
    QPointF a;
    QPointF b;
    QPointF control;
};

Loop selfLoop(const Shape &s, qreal radius)
{
    const QPointF a(s.x + s.w * 0.75, s.y);
    const QPointF b(s.x + s.w, s.y + s.h * 0.25);
    return {a, b, QPointF(b.x() + radius, a.y() - radius)};
}

QPointF quadraticPoint(QPointF p0, QPointF p1, QPointF p2, qreal t)
{
    const qreal u = 1 - t;
    return u * u * p0 + 2 * u * t * p1 + t * t * p2;
}

} // namespace

TransitionLink::TransitionLink()
    : Link(QStringLiteral("transition"))
{
}

qreal TransitionLink::parallelSpread(const Board &board) const
{
    // The generic 12 px tell two strokes apart, not two pills about 24 px
    // high: after a manual move (which drops the layout's curves) they
    // overlapped.
    return Link::parallelSpread(board) * 2.75;
}

qreal TransitionLink::selfOffset(const Board &board) const
{
    // The loops of the same state before this one, each separated by the
    // real heights of the two neighboring pills (multi-line labels too).
    std::vector<const Link *> siblings;
    for (const auto &l : board.links()) {
        if (dynamic_cast<const TransitionLink *>(l.get()) && l->from == from && l->to == to)
            siblings.push_back(l.get());
    }
    const auto it = std::find(siblings.begin(), siblings.end(), this);
    const qsizetype index = it == siblings.end() ? -1 : it - siblings.begin();
    qreal offset = 0;
    for (qsizetype i = 1; i <= index; ++i)
        offset += (labelHeight(board, *siblings[i - 1]) + labelHeight(board, *siblings[i])) / 2 + 8;
    return offset;
}

std::optional<Route> TransitionLink::route(const Board &board) const
{
    if (from != to)
        return Link::route(board);
    const Shape *s = board.shape(from);
    if (!s)
        return std::nullopt;
    const Loop loop = selfLoop(*s, 26 + selfOffset(board));
    // The loop as a polyline: what hit-testing and the bounds measure. It
    // is drawn as the quadratic itself.
    Route r;
    r.a = loop.a;
    r.b = loop.b;
    for (int i = 0; i <= 16; ++i)
        r.points.push_back(quadraticPoint(loop.a, loop.control, loop.b, i / 16.0));
    return r;
}

std::optional<QPointF> TransitionLink::validPlacement(const Board &board) const
{
    if (!placement)
        return std::nullopt;
    const Shape *a = board.shape(from);
    const Shape *b = board.shape(to);
    if (!a || !b || std::abs(a->x - placement->fromAnchor.x()) > 0.5 || std::abs(a->y - placement->fromAnchor.y()) > 0.5
        || std::abs(b->x - placement->toAnchor.x()) > 0.5 || std::abs(b->y - placement->toAnchor.y()) > 0.5) {
        placement.reset();
        return std::nullopt;
    }
    return placement->point;
}

QPointF TransitionLink::labelPos(const Route &r, const Board &board) const
{
    if (const std::optional<QPointF> placed = validPlacement(board))
        return *placed;
    if (from == to) {
        const qreal offset = selfOffset(board);
        return {r.b.x() + 34 + offset * 0.25, r.a.y() - 20 - offset};
    }
    return Link::labelPos(r, board);
}

void TransitionLink::draw(QPainter &painter, const Board &board, bool selected) const
{
    if (from != to) {
        Link::draw(painter, board, selected); // straight, or the layout's curve
        return;
    }
    const Shape *s = board.shape(from);
    if (!s)
        return;
    const LinkStyle style = drawStyle(board, selected);
    // A quadratic arc from the top edge, around the corner, to the right
    // edge; the head arrives along the arc's last tangent.
    const Loop loop = selfLoop(*s, 26 + selfOffset(board));
    const qreal angle = std::atan2(loop.b.y() - loop.control.y(), loop.b.x() - loop.control.x());
    const qreal inset = arrowBaseInset(style.arrow);
    QPainterPath path;
    path.moveTo(loop.a);
    path.quadTo(loop.control, QPointF(loop.b.x() - inset * std::cos(angle), loop.b.y() - inset * std::sin(angle)));
    stroke(painter, path, style.color, style.width, linkDash(*this));
    fillArrowHead(painter, loop.b, angle, style.arrow, style.color);
}

// ---------------------------------------------------------------------------
// Label resolution

namespace {

qreal overlapArea(const QRectF &a, const QRectF &b)
{
    return std::max<qreal>(0, std::min(a.right(), b.right()) - std::max(a.left(), b.left()))
           * std::max<qreal>(0, std::min(a.bottom(), b.bottom()) - std::max(a.top(), b.top()));
}

std::optional<QPointF> pointOnPolyline(const std::vector<QPointF> &points, qreal fraction)
{
    if (points.empty())
        return std::nullopt;
    if (points.size() == 1)
        return points.front();
    std::vector<qreal> lengths;
    qreal total = 0;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const qreal length = std::hypot(points[i + 1].x() - points[i].x(), points[i + 1].y() - points[i].y());
        lengths.push_back(length);
        total += length;
    }
    qreal remaining = total * fraction;
    for (size_t i = 0; i < lengths.size(); ++i) {
        if (remaining <= lengths[i] && lengths[i] > 0) {
            const qreal ratio = remaining / lengths[i];
            return points[i] + (points[i + 1] - points[i]) * ratio;
        }
        remaining -= lengths[i];
    }
    return points.back();
}

} // namespace

void resolveLabels(Board &board)
{
    std::vector<TransitionLink *> links;
    for (const auto &l : board.links()) {
        if (auto *t = dynamic_cast<TransitionLink *>(l.get()); t && !t->label.isEmpty())
            links.push_back(t);
    }
    for (TransitionLink *link : links)
        link->placement.reset();

    std::vector<QRectF> obstacles;
    for (const auto &s : board.shapes())
        obstacles.push_back(s->rect().adjusted(-9, -9, 9, 9));
    std::vector<QRectF> occupied;
    // Graphviz avoids its own text boxes, but the board paints wider pills
    // (padding, border, the real font): on a dense fan two distinct `lp` can
    // still overlap.
    for (TransitionLink *link : links) {
        const std::optional<Route> r = link->route(board);
        if (!r)
            continue;
        const Link::LabelBox size = link->labelBox(board);
        const auto boxAt = [&size](QPointF p) {
            return QRectF(p.x() - size.width / 2 - 5, p.y() - size.height / 2 - 5, size.width + 10, size.height + 10);
        };
        // After a layout, dot placed every label together and avoided their
        // collisions: that result is kept. This pass is for after a manual
        // move, when the curve and its `lp` no longer hold.
        if (r->labelPoint) {
            occupied.push_back(boxAt(*r->labelPoint));
            continue;
        }
        // The candidates all lie ON the route, from its middle outwards; a
        // self-loop keeps its own place.
        std::vector<std::pair<qreal, QPointF>> candidates;
        if (link->from != link->to) {
            const std::vector<QPointF> points = link->routePolyline(*r);
            for (const qreal fraction : {0.5, 0.4, 0.6, 0.3, 0.7, 0.2, 0.8, 0.12, 0.88}) {
                if (const std::optional<QPointF> p = pointOnPolyline(points, fraction))
                    candidates.emplace_back(fraction, *p);
            }
        }
        if (candidates.empty())
            candidates.emplace_back(0.5, link->labelPos(*r, board));
        std::optional<std::pair<QPointF, QRectF>> best;
        qreal bestScore = 0;
        for (const auto &[fraction, point] : candidates) {
            const QRectF box = boxAt(point);
            qreal collision = 0;
            for (const QRectF &obstacle : obstacles)
                collision += overlapArea(box, obstacle);
            for (const QRectF &previous : occupied)
                collision += overlapArea(box, previous) * 2;
            const qreal score = collision * 10000 + std::abs(fraction - 0.5) * 100;
            if (!best || score < bestScore) {
                best = {point, box};
                bestScore = score;
            }
        }
        const Shape *a = board.shape(link->from);
        const Shape *b = board.shape(link->to);
        if (!best || !a || !b)
            continue;
        link->placement = TransitionLink::Placement{best->first, QPointF(a->x, a->y), QPointF(b->x, b->y)};
        occupied.push_back(best->second);
    }
}

} // namespace diagram::states

// ---------------------------------------------------------------------------
// The family

namespace diagram::families {

const Family &states()
{
    using namespace diagram::states;
    static const Family family = [] {
        Family f;
        f.id = QStringLiteral("states");
        f.title = tr("State diagram");
        f.shapes = {
            {QStringLiteral("state"), [] { return std::unique_ptr<Shape>(new StateShape); }},
            {QStringLiteral("stateContainer"), [] { return std::unique_ptr<Shape>(new StateContainerShape); }},
            {QStringLiteral("initial"), [] { return std::unique_ptr<Shape>(new RoundShape(QStringLiteral("initial"), 18)); }},
            {QStringLiteral("final"), [] { return std::unique_ptr<Shape>(new RoundShape(QStringLiteral("final"), 22)); }},
            {QStringLiteral("choice"), [] { return std::unique_ptr<Shape>(new ChoiceShape); }},
            {QStringLiteral("fork"), [] { return std::unique_ptr<Shape>(new ForkJoinShape(QStringLiteral("fork"))); }},
            {QStringLiteral("join"), [] { return std::unique_ptr<Shape>(new ForkJoinShape(QStringLiteral("join"))); }},
        };
        // Self-transitions are idiomatic (A --> A : retry), and several events
        // can join the same pair of states.
        f.links = {{QStringLiteral("transition"), [] { return std::unique_ptr<Link>(new TransitionLink); }, true, true}};
        f.defaultShape = QStringLiteral("state");
        f.defaultLink = QStringLiteral("transition");
        f.resolveLabels = &diagram::states::resolveLabels;
        f.graphvizClusters = true;
        f.nestedCreation = true;
        f.hasDirection = true;
        // Pseudo-states carry no text: their UML symbol is their meaning.
        const auto preset = [](const char *id, const QString &label, const char *type, const QJsonObject &props) {
            return ElementPreset{QString::fromLatin1(id), label, {}, QString::fromLatin1(type), props};
        };
        f.elements = {
            preset("state-state", tr("State"), "state",
                   {{QStringLiteral("text"), tr("New state")}, {QStringLiteral("desc"), QJsonArray()}}),
            preset("state-container", tr("Composite state"), "stateContainer",
                   {{QStringLiteral("text"), tr("Composite state")}}),
            preset("state-initial", tr("Initial state"), "initial", {}),
            preset("state-final", tr("Final state"), "final", {}),
            preset("state-choice", tr("Choice"), "choice", {{QStringLiteral("text"), tr("Condition?")}}),
            preset("state-fork", tr("Fork"), "fork", {}),
            preset("state-join", tr("Join"), "join", {}),
        };
        return f;
    }();
    return family;
}

} // namespace diagram::families
