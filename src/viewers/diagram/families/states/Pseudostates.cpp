// SPDX-License-Identifier: GPL-3.0-only

#include "States.h"
#include "viewers/diagram/Board.h"

#include <QCoreApplication>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace diagram::states {

namespace {

const Font font(13);
constexpr qreal lineHeight = 20;

QString tr(const char *text)
{
    return QCoreApplication::translate("StateDiagram", text);
}

QPainterPath circle(QPointF center, qreal radius)
{
    return ellipse(center, radius, radius);
}

} // namespace

// ---------------------------------------------------------------------------
// RoundShape

RoundShape::RoundShape(const QString &typeName, qreal size)
    : StateElement(typeName, size, size)
{
}

bool RoundShape::hitTest(QPointF p) const
{
    const qreal dx = p.x() - cx();
    const qreal dy = p.y() - cy();
    return dx * dx + dy * dy <= (w / 2) * (w / 2);
}

QPointF RoundShape::borderPoint(QPointF toward) const
{
    const qreal dx = toward.x() - cx();
    const qreal dy = toward.y() - cy();
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
    const QColor color = state.selected ? style.shape.selectedStroke : fillColor(style.state.marker);
    if (type == QLatin1String("initial")) {
        painter.fillPath(outer, color);
        return;
    }
    // The final state: a ring around a bullet.
    QColor paper = style.background;
    if (const Shape *parent = board.containerAt(center)) {
        const QColor custom = parseColor(parent->customFill);
        paper = custom.isValid() ? custom : style.state.container;
    }
    painter.fillPath(outer, paper);
    stroke(painter, outer, color, state.selected ? 2.5 : 1.5);
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
    const QColor ink = board.style().state.ink;
    spec.color = readableTextColor(fillColor(board.style().state.header), ink, ink);
    spec.font = font;
    spec.lineHeight = lineHeight;
    spec.align = Align::Center;
    spec.rect = [this] { return QRectF(x + w * 0.22, y + h * 0.24, w * 0.56, h * 0.52); };
    board.beginTextEdit(this, spec);
}

QPointF ChoiceShape::borderPoint(QPointF toward) const
{
    const qreal dx = toward.x() - cx();
    const qreal dy = toward.y() - cy();
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
    painter.fillPath(path, fillColor(style.state.header));
    const QColor edge = state.selected ? style.shape.selectedStroke : strokeColor(style.state.stroke);
    stroke(painter, path, edge, state.selected ? 2.5 : 1.2);
    if (state.editing || text.isEmpty())
        return;
    painter.save();
    painter.setClipPath(path, Qt::IntersectClip);
    const QColor ink = readableTextColor(fillColor(style.state.header), style.state.ink, style.state.ink);
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

ForkJoinShape::ForkJoinShape(const QString &typeName)
    : StateElement(typeName, 100, 12)
{
}

QString ForkJoinShape::displayText() const
{
    return type == QLatin1String("fork") ? tr("Fork") : tr("Join");
}

void ForkJoinShape::draw(QPainter &painter, const Board &board, const ShapeState &state) const
{
    const Style &style = board.style();
    const QPainterPath path = roundRect(rect(), 3);
    const QColor color = state.selected ? style.shape.selectedStroke : fillColor(style.state.marker);
    painter.fillPath(path, color);
}

QList<Property> ForkJoinShape::properties() const
{
    return {{QStringLiteral("kind"),
             tr("Kind"),
             Property::Kind::Choice,
             {{QStringLiteral("fork"), tr("Fork")}, {QStringLiteral("join"), tr("Join")}}}};
}

QVariant ForkJoinShape::propertyValue(const QString &propertyId) const
{
    return propertyId == QLatin1String("kind") ? QVariant(type) : QVariant();
}

void ForkJoinShape::setPropertyValue(const QString &propertyId, const QVariant &value)
{
    // Same bar, other meaning: the type follows the choice.
    const bool isBarKind = value == QLatin1String("fork") || value == QLatin1String("join");
    if (propertyId == QLatin1String("kind") && isBarKind)
        type = value.toString();
}

} // namespace diagram::states
