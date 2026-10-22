// SPDX-License-Identifier: GPL-3.0-only

#include "Explainer.h"

#include "Presentation.h"
#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/families/Families.h"

#include <QHash>
#include <QJsonArray>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace diagram::explainer {

using namespace detail;

namespace {

const Font titleFont(14, 600);
constexpr qreal pad = 14;
constexpr qreal maxTextWidth = 280;
// Graphviz draws its `triangle` across the full width of a node box but only
// the top three quarters of its height, whatever the aspect ratio. Reserving a
// box a third taller makes its polygon exactly the triangle the board draws.
constexpr qreal graphvizTriangleFill = 0.75;

qreal frameStrokeWidth(const ShapeState &state)
{
    if (state.dropTarget)
        return 2.5;
    return state.selected ? 2 : 1.2;
}

} // namespace

const QStringList &shapeNames()
{
    static const QStringList names{QStringLiteral("rectangle"), QStringLiteral("rounded"),  QStringLiteral("circle"),
                                   QStringLiteral("ellipse"),   QStringLiteral("triangle"), QStringLiteral("diamond"),
                                   QStringLiteral("hexagon"),   QStringLiteral("text")};
    return names;
}

const QStringList &toneNames()
{
    static const QStringList names{QStringLiteral("neutral"), QStringLiteral("accent"),  QStringLiteral("info"),
                                   QStringLiteral("success"), QStringLiteral("warning"), QStringLiteral("danger")};
    return names;
}

const QStringList &arrowNames()
{
    static const QStringList names{QStringLiteral("none"), QStringLiteral("start"), QStringLiteral("end"),
                                   QStringLiteral("both")};
    return names;
}

const QStringList &lineNames()
{
    static const QStringList names{QStringLiteral("solid"), QStringLiteral("dashed"), QStringLiteral("dotted")};
    return names;
}

// ---------------------------------------------------------------------------
// FreeNodeShape

FreeNodeShape::FreeNodeShape()
    : Shape(QStringLiteral("freeNode"), 180, 76)
{
}

void FreeNodeShape::load(const QJsonObject &props)
{
    Shape::load(props);
    label = jsonString(props, QStringLiteral("label"), jsonString(props, QStringLiteral("text")));
    description = jsonString(props, QStringLiteral("description"));
    shape = oneOf(jsonString(props, QStringLiteral("shape")), shapeNames(), QStringLiteral("rounded"));
    tone = oneOf(jsonString(props, QStringLiteral("tone")), toneNames(), QStringLiteral("neutral"));
}

QJsonObject FreeNodeShape::toJson() const
{
    QJsonObject json = Shape::toJson();
    json.insert(QStringLiteral("label"), label);
    json.insert(QStringLiteral("description"), description);
    json.insert(QStringLiteral("shape"), shape);
    json.insert(QStringLiteral("tone"), tone);
    return json;
}

QRectF FreeNodeShape::textRect() const
{
    if (shape == QLatin1String("circle")) {
        const qreal width = safeTextWidth_.value_or(std::min(w, h) / std::numbers::sqrt2);
        const qreal height = safeTextHeight_.value_or(width);
        return {cx() - width / 2, cy() - height / 2, width, height};
    }
    if (shape == QLatin1String("ellipse")) {
        const qreal width = w / std::numbers::sqrt2;
        const qreal height = h / std::numbers::sqrt2;
        return {cx() - width / 2, cy() - height / 2, width, height};
    }
    if (shape == QLatin1String("triangle")) {
        const qreal width = w / 2.25;
        const qreal height = h / 2.2;
        return {cx() - width / 2, labelCenterY() - height / 2, width, height};
    }
    if (shape == QLatin1String("diamond"))
        return {cx() - w / 4, cy() - h / 4, w / 2, h / 2};
    if (shape == QLatin1String("hexagon")) {
        const qreal width = w / 1.5;
        const qreal height = h / 1.5;
        return {cx() - width / 2, cy() - height / 2, width, height};
    }
    return {x + pad, y + pad, std::max<qreal>(20, w - pad * 2), std::max(lineHeight, h - pad * 2)};
}

qreal FreeNodeShape::labelCenterY() const
{
    // A triangle's text sits low, where the outline is wide enough.
    return shape == QLatin1String("triangle") ? y + h * 0.68 : cy();
}

QList<QPointF> FreeNodeShape::polygonPoints() const
{
    const qreal r = x + w;
    const qreal b = y + h;
    if (shape == QLatin1String("triangle"))
        return {{cx(), y}, {r, b}, {x, b}};
    if (shape == QLatin1String("diamond"))
        return {{cx(), y}, {r, cy()}, {cx(), b}, {x, cy()}};
    if (shape == QLatin1String("hexagon"))
        return {{x + w * 0.25, y}, {x + w * 0.75, y}, {r, cy()}, {x + w * 0.75, b}, {x + w * 0.25, b}, {x, cy()}};
    return {};
}

QPainterPath FreeNodeShape::outline() const
{
    if (shape == QLatin1String("circle")) {
        const qreal radius = std::min(w, h) / 2;
        return ellipse(QPointF(cx(), cy()), radius, radius);
    }
    if (shape == QLatin1String("ellipse"))
        return ellipse(QPointF(cx(), cy()), w / 2, h / 2);
    const QList<QPointF> points = polygonPoints();
    if (!points.isEmpty())
        return polygon(points);
    // Every box of a diagram carries the same corner, whatever the family: a
    // free board is not the place to invent a second one.
    return roundRect(x, y, w, h, shapeRadiusSm);
}

QPointF FreeNodeShape::borderPoint(QPointF toward) const
{
    const QPointF center(cx(), cy());
    if (shape == QLatin1String("circle")) {
        const qreal radius = std::min(w, h) / 2;
        return ellipseBorderPoint(center, toward, radius, radius);
    }
    if (shape == QLatin1String("ellipse"))
        return ellipseBorderPoint(center, toward, w / 2, h / 2);
    const QList<QPointF> points = polygonPoints();
    return points.isEmpty() ? Shape::borderPoint(toward) : polygonBorderPoint(center, toward, points);
}

std::optional<QSizeF> FreeNodeShape::graphvizLayoutSize() const
{
    // Graphviz must route against the geometry the board actually draws: left
    // to its default box, a link to a circle or a diamond stops on that
    // invisible rectangle and floats visibly off the outline.
    if (shape != QLatin1String("triangle"))
        return std::nullopt;
    return QSizeF(w, h / graphvizTriangleFill);
}

QStringList FreeNodeShape::graphvizNodeAttributes(const Board &, const QString &, qreal width, qreal height) const
{
    // A shape with no outline of its own (text) routes against its text box.
    static const QHash<QString, QString> geometry{
        {QStringLiteral("rectangle"), QStringLiteral("box")},
        {QStringLiteral("rounded"), QStringLiteral("box")},
        {QStringLiteral("text"), QStringLiteral("box")},
        {QStringLiteral("circle"), QStringLiteral("circle")},
        {QStringLiteral("ellipse"), QStringLiteral("ellipse")},
        {QStringLiteral("triangle"), QStringLiteral("triangle")},
        {QStringLiteral("diamond"), QStringLiteral("diamond")},
        {QStringLiteral("hexagon"), QStringLiteral("hexagon")},
    };
    return {QStringLiteral("shape=") + geometry.value(shape, QStringLiteral("box")),
            QStringLiteral("fixedsize=true"),
            QStringLiteral("margin=0"),
            QStringLiteral("label=\"\""),
            QStringLiteral("width=") + QString::number(std::max<qreal>(1, width) / 72, 'f', 3),
            QStringLiteral("height=") + QString::number(std::max<qreal>(1, height) / 72, 'f', 3)};
}

void FreeNodeShape::measure(const Board &)
{
    // The outline grows with its text and keeps that room (the current size
    // is a floor) until it is explicitly resized; each geometry reserves the
    // area its text box needs inside it.
    const qreal currentWidth = w;
    const qreal currentHeight = h;
    const QStringList probe = text::wrap(font, label, maxTextWidth);
    const qreal safeWidth = std::max<qreal>(28, text::widest(font, probe) + pad * 2);
    const qreal safeHeight = std::max(lineHeight + pad * 2, probe.size() * lineHeight + pad * 2);
    safeTextWidth_ = safeWidth;
    safeTextHeight_ = safeHeight;
    const auto grow = [&](qreal minWidth, qreal minHeight) {
        minWidth_ = std::ceil(minWidth);
        minHeight_ = std::ceil(minHeight);
        w = std::max(currentWidth, minWidth_);
        h = std::max(currentHeight, minHeight_);
    };
    if (shape == QLatin1String("circle")) {
        minWidth_ = std::max<qreal>(96, std::ceil(std::hypot(safeWidth, safeHeight)));
        minHeight_ = minWidth_;
        w = h = std::max({currentWidth, currentHeight, minWidth_});
    } else if (shape == QLatin1String("ellipse")) {
        grow(std::max(140.0, safeWidth * std::numbers::sqrt2), std::max(78.0, safeHeight * std::numbers::sqrt2));
    } else if (shape == QLatin1String("triangle")) {
        grow(std::max(124.0, safeWidth * 2.25), std::max(108.0, safeHeight * 2.2));
    } else if (shape == QLatin1String("diamond")) {
        grow(std::max(132.0, safeWidth * 2), std::max(96.0, safeHeight * 2));
    } else if (shape == QLatin1String("hexagon")) {
        grow(std::max(150.0, safeWidth * 1.5), std::max(78.0, safeHeight * 1.5));
    } else if (shape == QLatin1String("text")) {
        grow(std::max(60.0, safeWidth), std::max(34.0, safeHeight));
    } else {
        grow(std::max(120.0, safeWidth), std::max(56.0, safeHeight));
    }
    lines_ = text::wrap(font, label, std::max<qreal>(20, textRect().width()));
}

void FreeNodeShape::resizeTo(qreal width, qreal height, const Board &)
{
    // Down to the text's floor, never below.
    if (shape == QLatin1String("circle")) {
        w = h = std::max({width, height, minWidth_, minHeight_});
    } else {
        w = std::max(width, minWidth_);
        h = std::max(height, minHeight_);
    }
    lines_ = text::wrap(font, label, std::max<qreal>(20, textRect().width()));
}

void FreeNodeShape::onDoubleClick(Board &board)
{
    TextEditSpec spec;
    spec.get = [this] { return label; };
    spec.set = [this](const QString &value) { label = value; };
    spec.color = board.style().palette.ink;
    spec.font = font;
    spec.lineHeight = lineHeight;
    spec.align = Align::Center;
    spec.rect = [this] { return textRect(); };
    board.beginTextEdit(this, spec);
}

void FreeNodeShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const bool bare = shape == QLatin1String("text");
    const QPainterPath path = outline();
    if (!bare) {
        shadowFill(painter, path, fillColor(toneFill(board, tone)), style);
        stroke(painter, path, strokeColor(state.selected ? style.shape.selectedStroke : style.shape.stroke),
               state.selected ? 2 : shapeBorderWidth);
    }
    if (state.editing)
        return;
    painter.save();
    if (!bare)
        painter.setClipPath(path, Qt::IntersectClip);
    const QColor ink = textColor(style.palette.ink);
    const QStringList lines = lines_.isEmpty() ? QStringList{QString()} : lines_;
    const auto drawLabel = [&](const QColor &color, QPointF offset) {
        qreal ty = labelCenterY() - ((lines.size() - 1) * lineHeight) / 2;
        for (const QString &line : lines) {
            text::draw(painter, font, line, QPointF(cx(), ty) + offset, color, Align::Center, Baseline::Middle);
            ty += lineHeight;
        }
    };
    // A text without an outline stands out from the paper by a small shadow.
    if (bare)
        smallShadow(board, drawLabel);
    drawLabel(ink, QPointF());
    painter.restore();
}

QList<Property> FreeNodeShape::properties() const
{
    return {
        {QStringLiteral("shape"),
         tr("Shape"),
         Property::Kind::Choice,
         {{QStringLiteral("rounded"), tr("Rounded box")},
          {QStringLiteral("rectangle"), tr("Rectangle")},
          {QStringLiteral("circle"), tr("Circle")},
          {QStringLiteral("ellipse"), tr("Ellipse")},
          {QStringLiteral("triangle"), tr("Triangle")},
          {QStringLiteral("diamond"), tr("Diamond")},
          {QStringLiteral("hexagon"), tr("Hexagon")},
          {QStringLiteral("text"), tr("Text")}}},
        {QStringLiteral("tone"), tr("Tone"), Property::Kind::Choice, toneChoices()},
        descriptionProperty(),
    };
}

QVariant FreeNodeShape::propertyValue(const QString &propertyId) const
{
    if (propertyId == QLatin1String("shape"))
        return shape;
    if (propertyId == QLatin1String("tone"))
        return tone;
    if (propertyId == QLatin1String("description"))
        return description;
    return {};
}

void FreeNodeShape::setPropertyValue(const QString &propertyId, const QVariant &value)
{
    if (propertyId == QLatin1String("shape")) {
        const QString next = oneOf(value.toString(), shapeNames(), shape);
        if (next == shape)
            return;
        shape = next;
        // Another geometry needs another floor: the measure recomputes it
        // from the text instead of keeping the old outline's size.
        manualWidth.reset();
        manualHeight.reset();
        if (shape == QLatin1String("circle"))
            w = h = std::min(w, h);
    } else if (propertyId == QLatin1String("tone")) {
        tone = oneOf(value.toString(), toneNames(), tone);
    } else if (propertyId == QLatin1String("description")) {
        description = value.toString();
    }
}

// ---------------------------------------------------------------------------
// FreeFrameShape

FreeFrameShape::FreeFrameShape()
    : Shape(QStringLiteral("freeFrame"), 360, 240)
{
}

void FreeFrameShape::load(const QJsonObject &props)
{
    Shape::load(props);
    title = jsonString(props, QStringLiteral("title"), jsonString(props, QStringLiteral("label")));
    description = jsonString(props, QStringLiteral("description"));
    tone = oneOf(jsonString(props, QStringLiteral("tone")), toneNames(), QStringLiteral("neutral"));
}

QJsonObject FreeFrameShape::toJson() const
{
    QJsonObject json = Shape::toJson();
    json.insert(QStringLiteral("title"), title);
    json.insert(QStringLiteral("description"), description);
    json.insert(QStringLiteral("tone"), tone);
    return json;
}

qreal FreeFrameShape::layoutLabelFontPx() const
{
    return font.px;
}

std::optional<QSizeF> FreeFrameShape::layoutMinimumSize() const
{
    return QSizeF(minWidth_, minHeight_);
}

void FreeFrameShape::wrapTitle()
{
    titleLines_ = text::wrap(titleFont, title, std::max<qreal>(20, w - pad * 2));
    titleHeight_ = std::max<qreal>(38, titleLines_.size() * lineHeight + 16);
}

void FreeFrameShape::measure(const Board &)
{
    const QStringList probe = text::wrap(titleFont, title, 480);
    minWidth_ = std::ceil(std::max<qreal>(220, text::widest(titleFont, probe) + pad * 2));
    minHeight_ = 140;
    w = std::ceil(std::max(w, minWidth_));
    h = std::ceil(std::max(h, minHeight_));
    wrapTitle();
    minHeight_ = std::max(minHeight_, titleHeight_ + 40);
    h = std::ceil(std::max(h, minHeight_));
}

std::optional<QRectF> FreeFrameShape::childBounds(const Board &) const
{
    return QRectF(QPointF(x + 12, y + titleHeight_ + 8), QPointF(x + w - 12, y + h - 12));
}

void FreeFrameShape::resizeTo(qreal width, qreal height, const Board &board)
{
    // Never smaller than its title, nor than the children it holds.
    const QList<Shape *> children = board.childShapes(this);
    qreal minWidth = minWidth_;
    for (const Shape *child : children)
        minWidth = std::max(minWidth, child->x + child->w - x + 12);
    w = std::max(width, minWidth);
    wrapTitle();
    minHeight_ = std::max<qreal>(140, titleHeight_ + 40);
    qreal minHeight = minHeight_;
    for (const Shape *child : children)
        minHeight = std::max(minHeight, child->y + child->h - y + 12);
    h = std::max(height, minHeight);
}

bool FreeFrameShape::hitTest(QPointF point) const
{
    // A frame is selected from its border and title, leaving its interior
    // free for panning and for the elements it visually groups.
    if (!Shape::hitTest(point))
        return false;
    constexpr qreal inset = 12;
    return point.y() <= y + titleHeight_ || point.x() <= x + inset || point.x() >= x + w - inset ||
           point.y() >= y + h - inset;
}

void FreeFrameShape::onDoubleClick(Board &board)
{
    TextEditSpec spec;
    spec.get = [this] { return title; };
    spec.set = [this](const QString &value) { title = value; };
    spec.color = board.style().palette.ink;
    spec.font = titleFont;
    spec.lineHeight = lineHeight;
    spec.align = Align::Left;
    spec.rect = [this] { return QRectF(x + pad, y + 8, w - pad * 2, std::max<qreal>(24, titleHeight_ - 12)); };
    board.beginTextEdit(this, spec);
}

void FreeFrameShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    // A frame about to take the drag turns solid: the dashed outline says
    // "this is a grouping", the solid one says "let go and it lands here".
    const bool dropTarget = state.dropTarget;
    const QPainterPath path = roundRect(x, y, w, h, shapeRadiusSm);
    painter.save();
    painter.setOpacity(painter.opacity() * (dropTarget ? 0.5 : 0.3));
    shadowFill(painter, path, fillColor(toneFill(board, tone)), style);
    painter.restore();
    stroke(painter, path, strokeColor(state.selected || dropTarget ? style.shape.selectedStroke : style.shape.stroke),
           frameStrokeWidth(state), dropTarget ? QList<qreal>{} : QList<qreal>{7, 5});
    if (state.editing || title.isEmpty())
        return;
    const QColor ink = textColor(style.palette.ink);
    const QStringList lines = titleLines_.isEmpty() ? QStringList{title} : titleLines_;
    qreal ty = y + 18;
    for (const QString &line : lines) {
        text::draw(painter, titleFont, line, QPointF(x + pad, ty), ink, Align::Left, Baseline::Middle);
        ty += lineHeight;
    }
}

QList<Property> FreeFrameShape::properties() const
{
    return {{QStringLiteral("tone"), tr("Tone"), Property::Kind::Choice, toneChoices()}, descriptionProperty()};
}

QVariant FreeFrameShape::propertyValue(const QString &propertyId) const
{
    if (propertyId == QLatin1String("tone"))
        return tone;
    if (propertyId == QLatin1String("description"))
        return description;
    return {};
}

void FreeFrameShape::setPropertyValue(const QString &propertyId, const QVariant &value)
{
    if (propertyId == QLatin1String("tone"))
        tone = oneOf(value.toString(), toneNames(), tone);
    else if (propertyId == QLatin1String("description"))
        description = value.toString();
}

// ---------------------------------------------------------------------------
// FreeAnchorShape

FreeAnchorShape::FreeAnchorShape()
    : Shape(QStringLiteral("freeAnchor"), 2, 2)
{
}

void FreeAnchorShape::load(const QJsonObject &props)
{
    Shape::load(props);
    sourceEdgeId = jsonString(props, QStringLiteral("sourceEdgeId"));
    endpoint = jsonString(props, QStringLiteral("endpoint"));
}

QJsonObject FreeAnchorShape::toJson() const
{
    QJsonObject json = Shape::toJson();
    json.insert(QStringLiteral("sourceEdgeId"), sourceEdgeId);
    json.insert(QStringLiteral("endpoint"), endpoint);
    return json;
}

} // namespace diagram::explainer

// ---------------------------------------------------------------------------
// The family

namespace diagram::families {

const Family &explainer()
{
    using namespace explainer;
    using namespace explainer::detail;
    static const Family family = [] {
        Family f;
        f.id = QStringLiteral("explainer");
        f.title = tr("Explainer board");
        f.shapes = {
            {QStringLiteral("freeNode"), [] { return std::unique_ptr<Shape>(new FreeNodeShape); }},
            {QStringLiteral("freeFrame"), [] { return std::unique_ptr<Shape>(new FreeFrameShape); }},
            {QStringLiteral("freeStroke"), [] { return std::unique_ptr<Shape>(new FreeStrokeShape); }},
            {QStringLiteral("freeAnchor"), [] { return std::unique_ptr<Shape>(new FreeAnchorShape); }},
        };
        LinkType edge{QStringLiteral("freeEdge"), [] { return std::unique_ptr<Link>(new FreeEdge); }};
        edge.allowMulti = true;
        f.links = {edge};
        f.defaultShape = QStringLiteral("freeNode");
        f.defaultLink = QStringLiteral("freeEdge");
        f.alignmentGuides = true;
        // Nothing but the pointer decides which frame owns an element here:
        // the source carries no layout intent the engine could defer to.
        f.reparentOnDrop = true;
        f.nestedCreation = true;
        f.graphvizClusters = true;
        // Most elements of an explainer carry no relation at all: without
        // this, Graphviz would rank them all together and spread the board
        // sideways.
        f.graphvizStackDisconnected = true;
        // Moving a shape invalidates the routes drawn around it; recomputing
        // them alone keeps a hand-arranged board exactly as its author left
        // it.
        f.routeLinksOnMove = true;
        f.inferClusterMembership = true;

        const auto node = [](const char *id, const QString &label, qreal w, qreal h, const QString &text,
                             const char *shape, const char *tone) {
            QJsonObject props{{QStringLiteral("w"), w},
                              {QStringLiteral("h"), h},
                              {QStringLiteral("label"), text},
                              {QStringLiteral("shape"), QString::fromLatin1(shape)}};
            if (tone)
                props.insert(QStringLiteral("tone"), QString::fromLatin1(tone));
            return ElementPreset{QString::fromLatin1(id), label, {}, QStringLiteral("freeNode"), props};
        };
        f.elements = {
            node("free-rounded", tr("Rounded box"), 180, 76, tr("New idea"), "rounded", "info"),
            node("free-rectangle", tr("Rectangle"), 180, 76, tr("New element"), "rectangle", nullptr),
            node("free-circle", tr("Circle"), 96, 96, tr("Concept"), "circle", "accent"),
            node("free-ellipse", tr("Ellipse"), 180, 88, tr("Concept"), "ellipse", "success"),
            node("free-triangle", tr("Triangle"), 124, 108, tr("Key point"), "triangle", "warning"),
            node("free-diamond", tr("Diamond"), 132, 96, tr("Question?"), "diamond", "warning"),
            node("free-hexagon", tr("Hexagon"), 180, 82, tr("Step"), "hexagon", "info"),
            node("free-text", tr("Text"), 180, 60, tr("Free text"), "text", nullptr),
            ElementPreset{QStringLiteral("free-frame"),
                          tr("Frame"),
                          {},
                          QStringLiteral("freeFrame"),
                          QJsonObject{{QStringLiteral("title"), tr("New section")},
                                      {QStringLiteral("tone"), QStringLiteral("neutral")}}},
            // Drawn by a press-drag-release: no arbitrary endpoints imposed.
            ElementPreset{QStringLiteral("free-segment"), tr("Draw a line"), tr("Then press and drag on the canvas"),
                          QStringLiteral("freeStroke"), QJsonObject{{QStringLiteral("arrow"), QStringLiteral("none")}},
                          true},
            ElementPreset{QStringLiteral("free-stroke"), tr("Draw an arrow"), tr("Then press and drag on the canvas"),
                          QStringLiteral("freeStroke"),
                          QJsonObject{{QStringLiteral("arrow"), QStringLiteral("end")},
                                      {QStringLiteral("tone"), QStringLiteral("accent")}},
                          true},
        };
        f.relations = {
            {QStringLiteral("free-arrow"),
             tr("Arrow"),
             {},
             QJsonObject{{QStringLiteral("arrow"), QStringLiteral("end")}}},
            {QStringLiteral("free-line"),
             tr("Line"),
             {},
             QJsonObject{{QStringLiteral("arrow"), QStringLiteral("none")}}},
        };
        return f;
    }();
    return family;
}

} // namespace diagram::families
