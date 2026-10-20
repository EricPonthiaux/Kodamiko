// SPDX-License-Identifier: GPL-3.0-only

#include "Sequence.h"

#include "Lanes.h"
#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/families/Families.h"

#include <QCoreApplication>
#include <QHash>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <limits>

namespace diagram::sequence {

using namespace detail;

namespace {

const Font font(14);
constexpr qreal lineHeight = 20;
constexpr qreal pad = 14;
constexpr qreal maxWidth = 320;

constexpr qreal lifelineTail = 28;   // lifeline overshoot below the last message
constexpr qreal selfLoopHeight = 18; // vertical drop of a self-message loop
constexpr qreal portAnchorGap = 14;  // the link handle right under the header

QString tr(const char *text)
{
    return QCoreApplication::translate("SequenceDiagram", text);
}

const QList<std::pair<QString, QString>> &kindChoices()
{
    static const QList<std::pair<QString, QString>> list{
        {QStringLiteral("participant"), tr("Participant")}, {QStringLiteral("actor"), tr("Actor")},
        {QStringLiteral("boundary"), tr("Boundary")},       {QStringLiteral("control"), tr("Control")},
        {QStringLiteral("entity"), tr("Entity")},           {QStringLiteral("database"), tr("Database")},
        {QStringLiteral("collections"), tr("Collections")}, {QStringLiteral("queue"), tr("Queue")},
    };
    return list;
}

} // namespace

// ---------------------------------------------------------------------------
// ParticipantShape

ParticipantShape::ParticipantShape()
    : Shape(QStringLiteral("participant"), 150, 44)
{
}

void ParticipantShape::load(const QJsonObject &props)
{
    Shape::load(props);
    text = jsonString(props, QStringLiteral("text"));
    kind = jsonString(props, QStringLiteral("kind"), QStringLiteral("participant"));
    if (kind.isEmpty())
        kind = QStringLiteral("participant");
    fill = jsonString(props, QStringLiteral("fill"), QStringLiteral("#ffffff"));
    if (fill.isEmpty())
        fill = QStringLiteral("#ffffff");
}

QJsonObject ParticipantShape::toJson() const
{
    QJsonObject json = Shape::toJson();
    json.insert(QStringLiteral("text"), text);
    json.insert(QStringLiteral("kind"), kind);
    json.insert(QStringLiteral("fill"), fill);
    return json;
}

void ParticipantShape::measure(const Board &)
{
    const QStringList probe = text::wrap(font, text, maxWidth - pad * 2);
    const qreal widest = std::max<qreal>(0, text::widest(font, probe));
    w = std::ceil(std::max<qreal>(120, std::min(maxWidth, widest + pad * 2)));
    lines_ = text::wrap(font, text, w - pad * 2);
    h = isActor() ? std::max<qreal>(80, 56 + lines_.size() * lineHeight)
                  : std::max<qreal>(44, lines_.size() * lineHeight + pad * 2);
}

void ParticipantShape::resizeTo(qreal width, qreal height, const Board &board)
{
    Shape::resizeTo(width, height, board);
    lines_ = text::wrap(font, text, std::max<qreal>(20, w - pad * 2));
}

// The creation handle lives on the lifeline, at the exact lane of the next
// message: the arrow appears where the user started it. On a diagram already
// filled (the one Claude writes), that lane is far below the header, out of
// reach of a hover over it, and dragging from the header only moves the
// shape: hence a second handle right under the header as soon as there is a
// message.
QList<Port> ParticipantShape::connectionPorts(const Board &board) const
{
    QList<Port> result{{cx(), messageY(board, nextMessageOrder(board)), {}}};
    if (maxOrder(board) >= 0)
        result.prepend({cx(), y + h + portAnchorGap, {}});
    return result;
}

bool ParticipantShape::connectionHitTest(const Board &board, QPointF point) const
{
    if (hitTest(point))
        return true;
    // sidecar divides the tolerance by the zoom; the board knows no camera
    // here, so it is in world units.
    constexpr qreal tolerance = 12;
    return std::abs(point.x() - cx()) <= tolerance && point.y() >= y + h && point.y() <= lifelineEnd(board);
}

QPointF ParticipantShape::connectionPoint(const Board &board, QPointF from, QPointF) const
{
    return {cx(), std::max(y + h, std::min(lifelineEnd(board), from.y()))};
}

qreal ParticipantShape::lifelineEnd(const Board &board) const
{
    return messageY(board, std::max(nextMessageOrder(board), 0)) + lifelineTail;
}

void ParticipantShape::onDoubleClick(Board &board)
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
        const qreal top = isActor() ? y + 55 : cy() - th / 2;
        return QRectF(x + pad, top, w - pad * 2, th);
    };
    board.beginTextEdit(this, spec);
}

// Activation bars: a `+` on a message opens one on its receiver, a `-`
// closes the innermost one of its sender (Mermaid's and PlantUML's meaning);
// a bar never closed runs to the last message.
void ParticipantShape::drawActivations(QPainter &painter, const Board &board) const
{
    const QList<const MessageLink *> list = orderedMessages(board);
    QList<int> starts;
    QList<std::pair<int, int>> intervals;
    int lastOrder = 0;
    for (const MessageLink *m : list) {
        lastOrder = std::max(lastOrder, m->order.value_or(0));
        if (m->activation == QLatin1String("+") && m->to == id)
            starts.append(m->order.value_or(0));
        if (m->activation == QLatin1String("-") && m->from == id && !starts.isEmpty())
            intervals.append({starts.takeLast(), m->order.value_or(0)});
    }
    while (!starts.isEmpty())
        intervals.append({starts.takeLast(), lastOrder});
    const Style &style = board.style();
    for (const auto &[start, end] : intervals) {
        const qreal top = messageY(board, start) - 6;
        const qreal bottom = messageY(board, end) + 12;
        const QRectF bar(cx() - 5, top, 10, std::max<qreal>(18, bottom - top));
        painter.fillRect(bar, style.palette.labelBg);
        QPainterPath path;
        path.addRect(bar);
        stroke(painter, path, style.palette.border, 1);
    }
}

void ParticipantShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    // The lifeline first, under the header.
    const qreal end = lifelineEnd(board);
    strokeLine(painter, QPointF(cx(), y + h), QPointF(cx(), std::max(end, y + h + lifelineTail)),
               style.palette.lifeline, 1.5, {5, 5});
    drawActivations(painter, board);
    if (isActor())
        drawActor(painter, style, state);
    else
        drawHeader(painter, style, state);
}

void ParticipantShape::drawActor(QPainter &painter, const Style &style, const ShapeState &state) const
{
    // The silhouette and its text share one color: the selection's, else
    // the user's fill, else the border's.
    const QColor custom = parseColor(customFill);
    QColor color = custom.isValid() ? custom : style.palette.border;
    if (state.selected)
        color = style.shape.selectedStroke;
    QPainterPath figure = ellipse(QPointF(cx(), y + 11), 7, 7);
    figure.moveTo(cx(), y + 18);
    figure.lineTo(cx(), y + 38);
    figure.moveTo(cx() - 13, y + 26);
    figure.lineTo(cx() + 13, y + 26);
    figure.moveTo(cx(), y + 38);
    figure.lineTo(cx() - 11, y + 50);
    figure.moveTo(cx(), y + 38);
    figure.lineTo(cx() + 11, y + 50);
    const qreal width = state.selected ? 2 : shapeBorderWidth;
    // sidecar's small shadow under the silhouette.
    stroke(painter, figure.translated(0, 2), style.palette.shadow, width + 1);
    stroke(painter, figure, strokeColor(color), width);
    if (state.editing)
        return;
    qreal ty = y + 56;
    for (const QString &line : lines_) {
        text::draw(painter, font, line, QPointF(cx(), ty), color, Align::Center, Baseline::Top);
        ty += lineHeight;
    }
}

void ParticipantShape::drawHeader(QPainter &painter, const Style &style, const ShapeState &state) const
{
    const QColor own = parseColor(fill);
    const QColor nativeFill = !own.isValid() || own == QColor(Qt::white) ? style.tones.cool : own;
    const QPainterPath path = roundRect(x, y, w, h, shapeRadiusSm);
    shadowFill(painter, path, fillColor(nativeFill), style);
    stroke(painter, path, strokeColor(state.selected ? style.shape.selectedStroke : style.palette.border),
           state.selected ? 2 : shapeBorderWidth);
    if (state.editing)
        return;
    const QColor ink = textColor(style.palette.ink);
    qreal ty = cy() - ((lines_.size() - 1) * lineHeight) / 2;
    for (const QString &line : lines_) {
        text::draw(painter, font, line, QPointF(cx(), ty), ink, Align::Center, Baseline::Middle);
        ty += lineHeight;
    }
}

// The real bounds for framing: the header and its lifeline down to the last
// message; without it the view would frame the row of headers alone.
std::optional<QRectF> ParticipantShape::bounds(const Board &board) const
{
    const qreal end = lifelineEnd(board);
    return QRectF(x, y, w, std::max(end, y + h) - y);
}

QList<Property> ParticipantShape::properties() const
{
    return {{QStringLiteral("kind"), tr("Kind"), Property::Kind::Choice, kindChoices()}};
}

QVariant ParticipantShape::propertyValue(const QString &propertyId) const
{
    if (propertyId == QLatin1String("kind"))
        return kind;
    return {};
}

void ParticipantShape::setPropertyValue(const QString &propertyId, const QVariant &value)
{
    if (propertyId != QLatin1String("kind"))
        return;
    kind = value.toString().isEmpty() ? QStringLiteral("participant") : value.toString();
    // An actor's silhouette is taller than a header box.
    manualWidth.reset();
    manualHeight.reset();
}

// ---------------------------------------------------------------------------
// MessageLink

MessageLink::MessageLink()
    : Link(QStringLiteral("message"))
{
}

void MessageLink::load(const QJsonObject &props)
{
    Link::load(props);
    const QJsonValue rank = props.value(QStringLiteral("order"));
    if (rank.isDouble())
        order = int(rank.toDouble());
    else
        order.reset();
    line = jsonString(props, QStringLiteral("line"), QStringLiteral("solid"));
    head = jsonString(props, QStringLiteral("head"), QStringLiteral("arrow"));
    startHead = jsonString(props, QStringLiteral("startHead"), QStringLiteral("none"));
    activation = jsonString(props, QStringLiteral("activation"));
    fragments.clear();
    for (const QJsonValue &value : props.value(QStringLiteral("fragments")).toArray()) {
        if (value.isObject())
            fragments.append(Fragment::fromJson(value.toObject()));
    }
}

QJsonObject MessageLink::toJson() const
{
    QJsonObject json = Link::toJson();
    json.insert(QStringLiteral("order"), order ? QJsonValue(*order) : QJsonValue(QJsonValue::Null));
    if (line != QLatin1String("solid"))
        json.insert(QStringLiteral("line"), line);
    if (head != QLatin1String("arrow"))
        json.insert(QStringLiteral("head"), head);
    if (startHead != QLatin1String("none"))
        json.insert(QStringLiteral("startHead"), startHead);
    if (!activation.isEmpty())
        json.insert(QStringLiteral("activation"), activation);
    if (!fragments.isEmpty()) {
        QJsonArray array;
        for (const Fragment &f : fragments)
            array.append(f.toJson());
        json.insert(QStringLiteral("fragments"), array);
    }
    return json;
}

std::optional<Route> MessageLink::route(const Board &board) const
{
    const Shape *a = board.shape(from);
    const Shape *b = board.shape(to);
    if (!a || !b)
        return std::nullopt;
    if (!order)
        order = maxOrder(board) + 1;
    const qreal lane = messageY(board, *order);
    Route r;
    r.a = QPointF(a->cx(), lane);
    if (from == to) {
        r.b = QPointF(a->cx(), lane + selfLoopHeight);
        r.points = {r.a, QPointF(a->cx() + selfLoopWidth, lane),
                    QPointF(a->cx() + selfLoopWidth, lane + selfLoopHeight), r.b};
        return r;
    }
    r.b = QPointF(b->cx(), lane);
    return r;
}

// Labels sit right on their message: the middle of the segment, or the far
// side of a self-message's loop.
QPointF MessageLink::labelPos(const Route &r, const Board &) const
{
    if (from == to)
        return {r.a.x() + selfLoopWidth, r.a.y() + selfLoopHeight / 2};
    return {(r.a.x() + r.b.x()) / 2, r.a.y()};
}

// A self-message's loop (and its label on the right) overflows the
// lifeline: declared for framing, or it falls off the screen.
std::optional<QRectF> MessageLink::bounds(const Board &board) const
{
    const std::optional<Route> r = route(board);
    if (!r || from != to)
        return std::nullopt;
    const qreal right = r->a.x() + selfLoopWidth + 34 + (label.isEmpty() ? 0 : label.size() * 4);
    return QRectF(QPointF(r->a.x(), r->a.y()), QPointF(right, r->b.y()));
}

void MessageLink::draw(QPainter &painter, const Board &board, bool selected) const
{
    const std::optional<Route> r = route(board);
    if (!r)
        return;
    const LinkStyle style = drawStyle(board, selected);
    BodyOptions body;
    body.color = style.color;
    body.width = style.width;
    if (line == QLatin1String("dashed"))
        body.dash = {6, 4};
    body.trim = head == QLatin1String("arrow") ? arrowBaseInset(style.arrow) : 0;
    body.trimStart = startHead == QLatin1String("arrow") ? arrowBaseInset(style.arrow) : 0;
    strokeBody(painter, *r, body);
    drawHead(painter, board, style, {head, r->b, arrivalAngle(*r)});
    if (startHead != QLatin1String("none"))
        drawHead(painter, board, style, {startHead, r->a, departureAngle(*r) + M_PI});
}

void MessageLink::drawHead(QPainter &painter,
                           const Board &board,
                           const LinkStyle &style,
                           const HeadPlacement &placement) const
{
    const QString &kind = placement.kind;
    const QPointF tip = placement.tip;
    const qreal angle = placement.angle;
    if (kind == QLatin1String("arrow")) {
        fillArrowHead(painter, tip, angle, style.arrow, style.color);
    } else if (kind == QLatin1String("open")) {
        const qreal spread = 0.5;
        const qreal length = (board.style().link.arrow + 2) * style.markerScale;
        QPainterPath path;
        path.moveTo(tip.x() - length * std::cos(angle - spread), tip.y() - length * std::sin(angle - spread));
        path.lineTo(tip);
        path.lineTo(tip.x() - length * std::cos(angle + spread), tip.y() - length * std::sin(angle + spread));
        stroke(painter, path, style.color, style.width);
    } else if (kind == QLatin1String("cross")) {
        const qreal s = 5 * style.markerScale;
        QPainterPath cross;
        cross.moveTo(tip.x() - s, tip.y() - s);
        cross.lineTo(tip.x() + s, tip.y() + s);
        cross.moveTo(tip.x() - s, tip.y() + s);
        cross.lineTo(tip.x() + s, tip.y() - s);
        stroke(painter, cross, style.color, style.width);
    }
}

QList<Property> MessageLink::properties() const
{
    const QList<std::pair<QString, QString>> heads{{QStringLiteral("arrow"), tr("Arrow")},
                                                   {QStringLiteral("open"), tr("Open arrow (async)")},
                                                   {QStringLiteral("cross"), tr("Cross (lost)")},
                                                   {QStringLiteral("none"), tr("None")}};
    QList<std::pair<QString, QString>> startHeads{{QStringLiteral("none"), tr("None")}};
    startHeads += heads.mid(0, 3);
    return {
        {QStringLiteral("line"),
         tr("Line"),
         Property::Kind::Choice,
         {{QStringLiteral("solid"), tr("Solid (call)")}, {QStringLiteral("dashed"), tr("Dashed (reply)")}}},
        {QStringLiteral("head"), tr("Head"), Property::Kind::Choice, heads},
        {QStringLiteral("startHead"), tr("Start head"), Property::Kind::Choice, startHeads},
        {QStringLiteral("activation"),
         tr("Activation"),
         Property::Kind::Choice,
         {{QString(), tr("None")},
          {QStringLiteral("+"), tr("Activate the receiver")},
          {QStringLiteral("-"), tr("Deactivate the sender")}}},
    };
}

QVariant MessageLink::propertyValue(const QString &propertyId) const
{
    if (propertyId == QLatin1String("line"))
        return line;
    if (propertyId == QLatin1String("head"))
        return head;
    if (propertyId == QLatin1String("startHead"))
        return startHead;
    if (propertyId == QLatin1String("activation"))
        return activation;
    return {};
}

void MessageLink::setPropertyValue(const QString &propertyId, const QVariant &value)
{
    if (propertyId == QLatin1String("line"))
        line = value.toString();
    else if (propertyId == QLatin1String("head"))
        head = value.toString();
    else if (propertyId == QLatin1String("startHead"))
        startHead = value.toString().isEmpty() ? QStringLiteral("none") : value.toString();
    else if (propertyId == QLatin1String("activation"))
        activation = value.toString();
}

} // namespace diagram::sequence

// ---------------------------------------------------------------------------
// The family

namespace diagram::families {

const Family &sequence()
{
    using namespace diagram::sequence;
    static const Family family = [] {
        Family f;
        f.id = QStringLiteral("sequence");
        f.title = tr("Sequence diagram");
        f.shapes = {{QStringLiteral("participant"), [] { return std::unique_ptr<Shape>(new ParticipantShape); }}};
        f.links = {{QStringLiteral("message"), [] { return std::unique_ptr<Link>(new MessageLink); }, true, true}};
        f.defaultShape = QStringLiteral("participant");
        f.defaultLink = QStringLiteral("message");
        // Messages route themselves along their lanes.
        f.routeLinksOnMove = false;
        f.autoArrange = arrange;
        f.drawUnderlay = drawFragments;
        const auto preset = [](const char *id, const QString &label, const QString &text, const char *kind) {
            return ElementPreset{
                QString::fromLatin1(id),
                label,
                {},
                QStringLiteral("participant"),
                QJsonObject{{QStringLiteral("text"), text}, {QStringLiteral("kind"), QLatin1String(kind)}}};
        };
        f.elements = {
            preset("sequence-participant", tr("Participant"), tr("New participant"), "participant"),
            preset("sequence-actor", tr("Actor"), tr("New actor"), "actor"),
        };
        return f;
    }();
    return family;
}

} // namespace diagram::families
