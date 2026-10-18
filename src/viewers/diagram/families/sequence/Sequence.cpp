#include "Sequence.h"

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

namespace {

const Font font(14);
const Font fragmentFont(11);
constexpr qreal lineHeight = 20;
constexpr qreal pad = 14;
constexpr qreal maxWidth = 320;

constexpr qreal messageStep = 44;    // vertical distance between consecutive messages
constexpr qreal lifelineTail = 28;   // lifeline overshoot below the last message
constexpr qreal selfLoopWidth = 56;  // horizontal reach of a self-message loop
constexpr qreal selfLoopHeight = 18; // vertical drop of a self-message loop
constexpr qreal portAnchorGap = 14;  // the link handle right under the header
constexpr qreal fragmentHeader = 24; // room a fragment's header reserves
constexpr qreal branchHeader = 22;   // room a branch separator reserves

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

QList<const MessageLink *> messages(const Board &board)
{
    QList<const MessageLink *> result;
    for (const auto &l : board.links()) {
        if (const auto *m = dynamic_cast<const MessageLink *>(l.get()))
            result.append(m);
    }
    return result;
}

// Messages in conversation order (a message without a rank counts as 0, as
// sidecar's `order ?? 0`).
QList<const MessageLink *> orderedMessages(const Board &board)
{
    QList<const MessageLink *> result = messages(board);
    std::stable_sort(result.begin(), result.end(), [](const MessageLink *a, const MessageLink *b) {
        return a->order.value_or(0) < b->order.value_or(0);
    });
    return result;
}

// Participants left to right.
QList<ParticipantShape *> participants(const Board &board)
{
    QList<ParticipantShape *> result;
    for (const auto &s : board.shapes()) {
        if (auto *p = dynamic_cast<ParticipantShape *>(s.get()))
            result.append(p);
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const ParticipantShape *a, const ParticipantShape *b) { return a->x < b->x; });
    return result;
}

// Lowest header bottom among participants: where the message lanes start.
qreal laneTop(const Board &board)
{
    qreal top = -std::numeric_limits<qreal>::infinity();
    for (const auto &s : board.shapes()) {
        if (dynamic_cast<const ParticipantShape *>(s.get()))
            top = std::max(top, s->y + s->h);
    }
    return std::isinf(top) ? 0 : top;
}

int maxOrder(const Board &board)
{
    int result = -1;
    for (const MessageLink *m : messages(board)) {
        if (m->order)
            result = std::max(result, *m->order);
    }
    return result;
}

bool sameBranch(const Fragment &a, const Fragment &b)
{
    return a.branchKeyword == b.branchKeyword && a.branchLabel == b.branchLabel;
}

// The room the fragments reserve above the message of rank `order`: a header
// for each fragment opened on the way, a separator for each branch switched.
qreal structureOffset(const Board &board, int order)
{
    QList<const MessageLink *> list;
    for (const MessageLink *m : orderedMessages(board)) {
        if (m->order.value_or(0) <= order)
            list.append(m);
    }
    QList<Fragment> active;
    qreal offset = 0;
    for (const MessageLink *m : list) {
        const QList<Fragment> &next = m->fragments;
        qsizetype common = 0;
        while (common < active.size() && common < next.size() && active[common].id == next[common].id)
            ++common;
        offset += std::max<qsizetype>(0, next.size() - common) * fragmentHeader;
        for (qsizetype i = 0; i < common; ++i) {
            if (!sameBranch(active[i], next[i]))
                offset += branchHeader;
        }
        active = next;
    }
    return offset;
}

// A fragment as drawn: its extent in ranks and its branches.
struct FragmentSpan {
    Fragment fragment;
    int startOrder = 0;
    int endOrder = 0;

    struct Branch {
        QString signature;
        int order = 0;
        QString keyword;
        QString label;
    };

    QList<Branch> branches;
};

QList<FragmentSpan> fragmentSpans(const Board &board)
{
    QList<FragmentSpan> found;
    QHash<QString, qsizetype> index;
    for (const MessageLink *m : messages(board)) {
        const int order = m->order.value_or(0);
        for (const Fragment &fragment : m->fragments) {
            qsizetype at = index.value(fragment.id, -1);
            if (at < 0) {
                at = found.size();
                index.insert(fragment.id, at);
                found.append({fragment, order, order, {}});
            }
            FragmentSpan &span = found[at];
            span.startOrder = std::min(span.startOrder, order);
            span.endOrder = std::max(span.endOrder, order);
            const QString signature = fragment.branchKeyword + QLatin1Char('\n') + fragment.branchLabel;
            const bool known = std::any_of(span.branches.cbegin(), span.branches.cend(),
                                           [&](const FragmentSpan::Branch &b) { return b.signature == signature; });
            if (!signature.trimmed().isEmpty() && !known)
                span.branches.append({signature, order, fragment.branchKeyword, fragment.branchLabel});
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const FragmentSpan &a, const FragmentSpan &b) {
        if (a.fragment.depth != b.fragment.depth)
            return a.fragment.depth < b.fragment.depth;
        return a.startOrder < b.startOrder;
    });
    return found;
}

QString joined(const QString &a, const QString &b)
{
    QStringList parts;
    if (!a.isEmpty())
        parts.append(a);
    if (!b.isEmpty())
        parts.append(b);
    return parts.join(QLatin1Char(' '));
}

} // namespace

// ---------------------------------------------------------------------------
// Fragments

QJsonObject Fragment::toJson() const
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("label"), label},
            {QStringLiteral("depth"), depth},
            {QStringLiteral("branchKeyword"), branchKeyword},
            {QStringLiteral("branchLabel"), branchLabel}};
}

Fragment Fragment::fromJson(const QJsonObject &json)
{
    Fragment f;
    f.id = jsonString(json, QStringLiteral("id"));
    f.kind = jsonString(json, QStringLiteral("kind"));
    f.label = jsonString(json, QStringLiteral("label"));
    f.depth = int(jsonNumber(json, QStringLiteral("depth"), 0));
    f.branchKeyword = jsonString(json, QStringLiteral("branchKeyword"));
    f.branchLabel = jsonString(json, QStringLiteral("branchLabel"));
    return f;
}

qreal messageY(const Board &board, int order)
{
    return laneTop(board) + messageStep * (order + 1) + structureOffset(board, order);
}

int nextMessageOrder(const Board &board)
{
    return maxOrder(board) + 1;
}

QList<Fragment> syncFragments(QStringList &out,
                              const QList<Fragment> &active,
                              const QList<Fragment> &next,
                              const QString &indent,
                              bool parallelBranches,
                              const std::function<QString(const QString &)> &encode)
{
    const auto text = [&](const QString &value) { return encode ? encode(value) : value; };
    qsizetype common = 0;
    while (common < active.size() && common < next.size() && active[common].id == next[common].id)
        ++common;
    for (qsizetype i = active.size() - 1; i >= common; --i)
        out.append(indent + QStringLiteral("end"));
    for (qsizetype i = common; i < next.size(); ++i) {
        const Fragment &f = next[i];
        out.append(indent + f.kind + (f.label.isEmpty() ? QString() : QLatin1Char(' ') + text(f.label)));
    }
    for (qsizetype i = 0; i < common; ++i) {
        const Fragment &before = active[i];
        const Fragment &after = next[i];
        if (sameBranch(before, after))
            continue;
        const QString keyword = !after.branchKeyword.isEmpty()                           ? after.branchKeyword
                                : parallelBranches && after.kind == QLatin1String("par") ? QStringLiteral("and")
                                                                                         : QStringLiteral("else");
        out.append(indent + keyword +
                   (after.branchLabel.isEmpty() ? QString() : QLatin1Char(' ') + text(after.branchLabel)));
    }
    return next;
}

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

    if (isActor()) {
        const QColor custom = parseColor(customFill);
        const QColor color = state.selected     ? style.shape.selectedStroke
                             : custom.isValid() ? custom
                                                : style.palette.border;
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
        return;
    }

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

QVariant ParticipantShape::propertyValue(const QString &id) const
{
    if (id == QLatin1String("kind"))
        return kind;
    return {};
}

void ParticipantShape::setPropertyValue(const QString &id, const QVariant &value)
{
    if (id != QLatin1String("kind"))
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
    drawHead(painter, board, r->b, arrivalAngle(*r), style, head);
    if (startHead != QLatin1String("none"))
        drawHead(painter, board, r->a, departureAngle(*r) + M_PI, style, startHead);
}

void MessageLink::drawHead(QPainter &painter,
                           const Board &board,
                           QPointF tip,
                           qreal angle,
                           const LinkStyle &style,
                           const QString &kind) const
{
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

QVariant MessageLink::propertyValue(const QString &id) const
{
    if (id == QLatin1String("line"))
        return line;
    if (id == QLatin1String("head"))
        return head;
    if (id == QLatin1String("startHead"))
        return startHead;
    if (id == QLatin1String("activation"))
        return activation;
    return {};
}

void MessageLink::setPropertyValue(const QString &id, const QVariant &value)
{
    if (id == QLatin1String("line"))
        line = value.toString();
    else if (id == QLatin1String("head"))
        head = value.toString();
    else if (id == QLatin1String("startHead"))
        startHead = value.toString().isEmpty() ? QStringLiteral("none") : value.toString();
    else if (id == QLatin1String("activation"))
        activation = value.toString();
}

// ---------------------------------------------------------------------------
// Layout and underlay

// A dedicated layout: a sequence is not a layered graph, Graphviz would
// scatter it. The participants line up on one row in their current
// left-to-right order, lifelines spaced so that every message label fits
// between its two ends, and the lanes are compacted (the gaps left by
// deletions disappear).
void arrange(const Board &board)
{
    const QList<ParticipantShape *> parts = participants(board);
    if (parts.isEmpty())
        return;
    QHash<QString, int> index;
    for (int i = 0; i < parts.size(); ++i)
        index.insert(parts[i]->id, i);

    // Compacted lanes: the relative order is kept, the holes disappear.
    QList<const MessageLink *> list = messages(board);
    std::stable_sort(list.begin(), list.end(), [](const MessageLink *a, const MessageLink *b) {
        constexpr int last = std::numeric_limits<int>::max();
        return a->order.value_or(last) < b->order.value_or(last);
    });
    for (int i = 0; i < list.size(); ++i)
        list[i]->order = i;

    // Spacing: at least minGap everywhere; each message claims its label's
    // width between its lifelines (spread over the intervals it crosses),
    // and a self-message the room of its loop on the right.
    const Font labelFont(board.style().linkLabel.fontPx);
    constexpr qreal minGap = 70;
    QList<qreal> gaps(std::max<qsizetype>(parts.size() - 1, 0), minGap);
    const auto spanOf = [&](const MessageLink *l) { return std::abs(index.value(l->from, 0) - index.value(l->to, 0)); };
    QList<const MessageLink *> bySpan = list;
    std::stable_sort(bySpan.begin(), bySpan.end(),
                     [&](const MessageLink *a, const MessageLink *b) { return spanOf(a) < spanOf(b); });
    for (const MessageLink *l : bySpan) {
        if (!index.contains(l->from) || !index.contains(l->to))
            continue;
        const int i = index.value(l->from);
        const int j = index.value(l->to);
        const qreal labelWidth =
            l->label.isEmpty() ? 0 : text::width(labelFont, l->label.section(QLatin1Char('\n'), 0, 0)) + 30;
        if (i == j) {
            const qreal need = selfLoopWidth + labelWidth + 20;
            if (i < gaps.size())
                gaps[i] = std::max(gaps[i], need);
            continue;
        }
        const int lo = std::min(i, j);
        const int hi = std::max(i, j);
        qreal centerDistance = 0;
        for (int k = lo; k < hi; ++k)
            centerDistance += parts[k]->w / 2 + gaps[k] + parts[k + 1]->w / 2;
        if (labelWidth > centerDistance) {
            const qreal extra = (labelWidth - centerDistance) / (hi - lo);
            for (int k = lo; k < hi; ++k)
                gaps[k] += extra;
        }
    }

    // One row of headers, aligned at the top.
    qreal x = 0;
    for (int i = 0; i < parts.size(); ++i) {
        parts[i]->x = x;
        parts[i]->y = 20;
        x += parts[i]->w + (i < gaps.size() ? gaps[i] : 0);
    }
}

void drawFragments(QPainter &painter, const Board &board)
{
    const QList<ParticipantShape *> parts = participants(board);
    if (parts.isEmpty())
        return;
    const Style &style = board.style();
    const qreal left = parts.front()->cx() - 34;
    const qreal right = parts.back()->cx() + 34;
    for (const FragmentSpan &span : fragmentSpans(board)) {
        const qreal inset = span.fragment.depth * 8;
        const qreal x = left + inset;
        const qreal width = std::max<qreal>(80, right - left - inset * 2);
        const qreal top = messageY(board, span.startOrder) - 44;
        const qreal bottom = messageY(board, span.endOrder) + 22;
        stroke(painter, roundRect(x, top, width, bottom - top, shapeRadiusSm), style.palette.separator, 1.25);

        const QString heading = joined(span.fragment.kind, span.fragment.label);
        if (!heading.isEmpty()) {
            const qreal headingWidth = text::width(fragmentFont, heading) + 16;
            painter.fillRect(QRectF(x + 1, top + 1, headingWidth, 19), style.palette.labelBg);
            text::draw(painter, fragmentFont, heading, QPointF(x + 8, top + 10), style.palette.labelInk, Align::Left,
                       Baseline::Middle);
        }
        for (const FragmentSpan::Branch &branch : span.branches) {
            if (branch.order <= span.startOrder)
                continue;
            const qreal y = messageY(board, branch.order) - 31;
            strokeLine(painter, QPointF(x, y), QPointF(x + width, y), style.palette.separator, 1.25, {5, 4});
            const QString label = joined(branch.keyword, branch.label);
            if (!label.isEmpty()) {
                const qreal labelWidth = text::width(fragmentFont, label) + 12;
                painter.fillRect(QRectF(x + 8, y - 8, labelWidth, 16), style.palette.labelBg);
                text::draw(painter, fragmentFont, label, QPointF(x + 14, y), style.palette.labelInk, Align::Left,
                           Baseline::Middle);
            }
        }
    }
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
