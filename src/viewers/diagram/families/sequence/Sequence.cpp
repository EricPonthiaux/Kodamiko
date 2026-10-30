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

const Font font(17, 600);
constexpr qreal lineHeight = 24;
constexpr qreal pad = 18;
constexpr qreal maxWidth = 320;

constexpr qreal lifelineTail = 28;  // lifeline overshoot below the last message
constexpr qreal portAnchorGap = 14; // the link handle right under the header

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
    : Shape(QStringLiteral("participant"), 180, 64)
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
    w = std::ceil(std::max<qreal>(180, std::min(maxWidth, widest + pad * 2)));
    lines_ = text::wrap(font, text, w - pad * 2);
    h = isActor() ? std::max<qreal>(80, 56 + lines_.size() * lineHeight)
                  : std::max<qreal>(64, lines_.size() * lineHeight + pad * 2);
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
    spec.color = isActor() ? board.style().sequence.ink : headerInk(board.style());
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
    const Style &style = board.style();
    for (const ActivationSpan &span : activationSpans(board, id)) {
        const qreal top = messageY(board, span.start);
        const qreal bottom = activationBottom(board, span);
        const QRectF bar(cx() - 5 + span.depth * 4, top, 10, std::max<qreal>(18, bottom - top));
        const QPainterPath path = roundRect(bar, 2);
        painter.fillPath(path, headerFill(style));
        stroke(painter, path, style.sequence.stroke, 1.2);
    }
}

void ParticipantShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    // The lifeline first, under the header.
    const qreal end = lifelineEnd(board);
    strokeLine(painter, QPointF(cx(), y + h), QPointF(cx(), std::max(end, y + h + lifelineTail)),
               style.sequence.lifeline, 1.2, {3, 7});
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
    QColor color = custom.isValid() ? custom : style.sequence.stroke;
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
    const qreal width = state.selected ? 2.5 : 1.5;
    stroke(painter, figure, color, width);
    if (state.editing)
        return;
    qreal ty = y + 56;
    for (const QString &line : lines_) {
        text::draw(painter, font, line, QPointF(cx(), ty), style.sequence.ink, Align::Center, Baseline::Top);
        ty += lineHeight;
    }
}

QColor ParticipantShape::headerFill(const Style &style) const
{
    const QColor own = parseColor(fill);
    const QColor nativeFill = !own.isValid() || own == QColor(Qt::white) ? style.sequence.header : own;
    return fillColor(nativeFill);
}

QColor ParticipantShape::headerInk(const Style &style) const
{
    return readableTextColor(headerFill(style), style.sequence.ink, style.sequence.ink);
}

void ParticipantShape::drawHeader(QPainter &painter, const Style &style, const ShapeState &state) const
{
    const QPainterPath path = roundRect(rect(), 8);
    painter.fillPath(path, headerFill(style));
    const QColor edge = state.selected ? style.shape.selectedStroke : strokeColor(style.sequence.stroke);
    stroke(painter, path, edge, state.selected ? 2.5 : 1.2);
    if (state.editing)
        return;
    const QColor ink = headerInk(style);
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
    QRectF bounds(x, y, w, std::max(end, y + h) - y);
    if (!board.shapes().empty() && board.shapes().front().get() == this) {
        if (const auto frames = fragmentBounds(board))
            bounds = bounds.united(*frames);
    }
    return bounds;
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
        f.drawOverlay = drawFragmentLabels;
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
