// SPDX-License-Identifier: GPL-3.0-only

#include "SketchPainting.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace SketchPainting {

namespace {

constexpr qreal outlineWidth = 1.5;
constexpr qreal cornerRadius = 6;
constexpr qreal textBarHeight = 3;
constexpr qreal labelWidth = 30;
constexpr qreal penRadius = 3.5;
constexpr qreal arrowLength = 7;
constexpr qreal arrowHalfWidth = 3.5;
constexpr qreal triangleLength = 10;
constexpr qreal triangleHalfWidth = 6;
constexpr qreal crowFootLength = 9;
constexpr qreal crowFootHalfWidth = 5;
constexpr qreal lifelineHeadHeight = 24;
constexpr qreal cylinderLidHeight = 10;
constexpr qreal badgeRadius = 9;
constexpr int curveSamples = 32;
// A node grows to its size from this scale as it appears.
constexpr qreal appearingScale = 0.8;

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(alpha);
    return color;
}

qreal easeOut(qreal t)
{
    return 1 - std::pow(1 - t, 3);
}

// The shapes' outline and fill.
void useShapeInk(QPainter &painter, const Ink &ink, qreal fillAlpha = 0.12)
{
    painter.setPen(QPen(ink.accent, outlineWidth));
    painter.setBrush(withAlpha(ink.accent, fillAlpha));
}

// A bar `widthShare` of `rect`'s width, from its left plus `inset`, standing
// for a line of text centered on `y`.
void drawTextBar(QPainter &painter, const QRectF &rect, qreal y, qreal widthShare)
{
    const qreal inset = std::min(10.0, rect.width() * 0.15);
    painter.drawRoundedRect(
        QRectF(rect.left() + inset, y - textBarHeight / 2, rect.width() * widthShare, textBarHeight), textBarHeight / 2,
        textBarHeight / 2);
}

// Two lines of blank text centered in `rect`.
void drawText(QPainter &painter, const QRectF &rect, const Ink &ink)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink.text);
    const qreal width = rect.width() * 0.6;
    const QRectF centered(rect.center().x() - width / 2, rect.top(), width, rect.height());
    painter.drawRoundedRect(QRectF(centered.left(), rect.center().y() - textBarHeight * 1.5, width, textBarHeight),
                            textBarHeight / 2, textBarHeight / 2);
    painter.drawRoundedRect(
        QRectF(centered.left(), rect.center().y() + textBarHeight * 0.5, width * 0.6, textBarHeight), textBarHeight / 2,
        textBarHeight / 2);
}

// A header band of sketchHeaderHeight at the top of `outline`, and its title.
void drawHeader(QPainter &painter, const QPainterPath &outline, const QRectF &rect, const Ink &ink)
{
    const QRectF header(rect.left(), rect.top(), rect.width(), sketchHeaderHeight);
    painter.save();
    painter.setClipPath(outline);
    painter.fillRect(header, withAlpha(ink.accent, 0.28));
    painter.restore();
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink.text);
    const qreal width = rect.width() * 0.45;
    painter.drawRoundedRect(
        QRectF(header.center().x() - width / 2, header.center().y() - textBarHeight / 2, width, textBarHeight),
        textBarHeight / 2, textBarHeight / 2);
}

// `count` rows under the header of `rect`, each a key and a name.
void drawRows(QPainter &painter, const QRectF &rows, int count, const Ink &ink)
{
    const qreal rowHeight = rows.height() / count;
    for (int i = 0; i < count; ++i) {
        const qreal y = rows.top() + (i + 0.5) * rowHeight;
        if (i > 0) {
            painter.setPen(QPen(withAlpha(ink.accent, 0.3), 1));
            painter.drawLine(QPointF(rows.left(), rows.top() + i * rowHeight),
                             QPointF(rows.right(), rows.top() + i * rowHeight));
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink.text);
        drawTextBar(painter, rows, y, i % 2 == 0 ? 0.55 : 0.4);
    }
}

void drawTable(QPainter &painter, const SketchNode &node, const Ink &ink)
{
    QPainterPath outline;
    outline.addRoundedRect(node.rect, cornerRadius, cornerRadius);
    useShapeInk(painter, ink, 0.06);
    painter.drawPath(outline);
    drawHeader(painter, outline, node.rect, ink);
    drawRows(painter, node.rect.adjusted(0, sketchHeaderHeight, 0, 0), std::max(1, node.count), ink);
}

// A name, then a compartment of attributes and one of methods.
void drawClass(QPainter &painter, const SketchNode &node, const Ink &ink)
{
    QPainterPath outline;
    outline.addRoundedRect(node.rect, 3, 3);
    useShapeInk(painter, ink, 0.06);
    painter.drawPath(outline);
    drawHeader(painter, outline, node.rect, ink);
    const QRectF body = node.rect.adjusted(0, sketchHeaderHeight, 0, 0);
    const QRectF attributes(body.topLeft(), QSizeF(body.width(), body.height() / 2));
    const QRectF methods = attributes.translated(0, attributes.height());
    painter.setPen(QPen(withAlpha(ink.accent, 0.5), 1));
    painter.drawLine(methods.topLeft(), methods.topRight());
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink.text);
    for (const QRectF &compartment : {attributes, methods}) {
        const int count = std::max(1, node.count);
        for (int i = 0; i < count; ++i)
            drawTextBar(painter, compartment, compartment.top() + (i + 0.5) * compartment.height() / count,
                        i % 2 == 0 ? 0.5 : 0.65);
    }
}

void drawPerson(QPainter &painter, const SketchNode &node, const Ink &ink)
{
    const qreal headRadius = node.rect.width() * 0.16;
    const QPointF head(node.rect.center().x(), node.rect.top() + headRadius);
    const QRectF body = node.rect.adjusted(0, headRadius * 2 + 3, 0, 0);
    useShapeInk(painter, ink);
    painter.drawEllipse(head, headRadius, headRadius);
    painter.drawRoundedRect(body, body.height() / 2.5, body.height() / 2.5);
    drawText(painter, body, ink);
}

void drawBoundary(QPainter &painter, const SketchNode &node, const Ink &ink)
{
    QPen pen(withAlpha(ink.accent, 0.8), 1.2, Qt::DashLine);
    pen.setDashPattern({5, 4});
    painter.setPen(pen);
    painter.setBrush(withAlpha(ink.accent, 0.04));
    painter.drawRoundedRect(node.rect, cornerRadius, cornerRadius);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink.text);
    drawTextBar(painter, QRectF(node.rect.left(), node.rect.top(), 90, 0), node.rect.bottom() - 10, 0.7);
}

void drawCylinder(QPainter &painter, const SketchNode &node, const Ink &ink)
{
    const QRectF &rect = node.rect;
    const QRectF lid(rect.left(), rect.top(), rect.width(), cylinderLidHeight);
    const QRectF base(rect.left(), rect.bottom() - cylinderLidHeight, rect.width(), cylinderLidHeight);
    QPainterPath body;
    body.moveTo(lid.left(), lid.center().y());
    body.arcTo(base, 180, 180);
    body.lineTo(lid.right(), lid.center().y());
    body.arcTo(lid, 0, 180);
    useShapeInk(painter, ink);
    painter.drawPath(body);
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(lid, 180 * 16, 180 * 16);
    drawText(painter, rect.adjusted(0, cylinderLidHeight / 2, 0, 0), ink);
}

// A participant's head, and the dashed line of its life below.
void drawLifeline(QPainter &painter, const SketchNode &node, const Ink &ink)
{
    const QRectF head(node.rect.topLeft(), QSizeF(node.rect.width(), lifelineHeadHeight));
    QPen pen(ink.line, 1, Qt::DashLine);
    pen.setDashPattern({4, 4});
    painter.setPen(pen);
    painter.drawLine(QPointF(head.center().x(), head.bottom()), QPointF(head.center().x(), node.rect.bottom()));
    useShapeInk(painter, ink);
    painter.drawRoundedRect(head, cornerRadius, cornerRadius);
    drawText(painter, head, ink);
}

// A numbered frame with a title, a small picture and a caption.
void drawPanel(QPainter &painter, const SketchNode &node, const Ink &ink)
{
    const QRectF &rect = node.rect;
    useShapeInk(painter, ink, 0.05);
    painter.drawRoundedRect(rect, cornerRadius + 2, cornerRadius + 2);

    const QPointF badge = rect.topLeft() + QPointF(badgeRadius + 8, badgeRadius + 8);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink.accent);
    painter.drawEllipse(badge, badgeRadius, badgeRadius);
    QFont font = painter.font();
    font.setPixelSize(int(badgeRadius * 1.3));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(ink.surface);
    painter.drawText(QRectF(badge - QPointF(badgeRadius, badgeRadius), QSizeF(badgeRadius * 2, badgeRadius * 2)),
                     Qt::AlignCenter, QString::number(node.count));

    painter.setPen(Qt::NoPen);
    painter.setBrush(ink.text);
    drawTextBar(painter, rect.adjusted(badgeRadius * 2 + 4, 0, 0, 0), badge.y(), 0.55);

    // The picture: two boxes, one over the other, joined.
    const QRectF upper(rect.center().x() - 34, rect.top() + 46, 68, 24);
    const QRectF lower = upper.translated(0, 46);
    painter.setPen(QPen(ink.line, 1));
    painter.drawLine(QPointF(upper.center().x(), upper.bottom()), QPointF(lower.center().x(), lower.top()));
    useShapeInk(painter, ink, 0.18);
    painter.drawRoundedRect(upper, 4, 4);
    painter.drawRoundedRect(lower, 4, 4);

    painter.setPen(Qt::NoPen);
    painter.setBrush(ink.text);
    drawTextBar(painter, rect, rect.bottom() - 28, 0.7);
    drawTextBar(painter, rect, rect.bottom() - 18, 0.5);
}

void drawDecision(QPainter &painter, const QRectF &rect, const Ink &ink)
{
    QPainterPath diamond;
    diamond.moveTo(rect.center().x(), rect.top());
    diamond.lineTo(rect.right(), rect.center().y());
    diamond.lineTo(rect.center().x(), rect.bottom());
    diamond.lineTo(rect.left(), rect.center().y());
    diamond.closeSubpath();
    useShapeInk(painter, ink);
    painter.drawPath(diamond);
}

void drawEndDot(QPainter &painter, const QRectF &rect, const Ink &ink)
{
    painter.setPen(QPen(ink.accent, outlineWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(rect);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink.accent);
    painter.drawEllipse(rect.adjusted(4, 4, -4, -4));
}

void drawShape(QPainter &painter, const SketchNode &node, const Ink &ink)
{
    const QRectF &rect = node.rect;
    switch (node.shape) {
    case SketchShape::Terminal:
        useShapeInk(painter, ink);
        painter.drawRoundedRect(rect, rect.height() / 2, rect.height() / 2);
        drawText(painter, rect, ink);
        return;
    case SketchShape::Box:
        useShapeInk(painter, ink);
        painter.drawRoundedRect(rect, cornerRadius, cornerRadius);
        drawText(painter, rect, ink);
        return;
    case SketchShape::Decision:
        drawDecision(painter, rect, ink);
        return;
    case SketchShape::StartDot:
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink.accent);
        painter.drawEllipse(rect);
        return;
    case SketchShape::EndDot:
        drawEndDot(painter, rect, ink);
        return;
    case SketchShape::Ellipse:
        useShapeInk(painter, ink, 0.2);
        painter.drawEllipse(rect);
        drawText(painter, rect, ink);
        return;
    case SketchShape::Table:
        drawTable(painter, node, ink);
        return;
    case SketchShape::Class:
        drawClass(painter, node, ink);
        return;
    case SketchShape::Person:
        drawPerson(painter, node, ink);
        return;
    case SketchShape::Boundary:
        drawBoundary(painter, node, ink);
        return;
    case SketchShape::Cylinder:
        drawCylinder(painter, node, ink);
        return;
    case SketchShape::Lifeline:
        drawLifeline(painter, node, ink);
        return;
    case SketchShape::Activation:
        useShapeInk(painter, ink, 0.35);
        painter.drawRect(rect);
        return;
    case SketchShape::Band:
        painter.setPen(Qt::NoPen);
        painter.setBrush(withAlpha(ink.accent, 0.28));
        painter.drawRoundedRect(rect, 4, 4);
        drawText(painter, rect.adjusted(0, 3, 0, 0), ink);
        return;
    case SketchShape::Panel:
        drawPanel(painter, node, ink);
        return;
    }
}

// The first `progress` of `path`, as a polyline.
QPainterPath partOf(const QPainterPath &path, qreal progress)
{
    QPainterPath part(path.pointAtPercent(0));
    const int samples = std::max(1, int(std::ceil(progress * curveSamples)));
    for (int i = 1; i <= samples; ++i)
        part.lineTo(path.pointAtPercent(std::min(progress, qreal(i) / curveSamples)));
    return part;
}

// The unit vector along which `path` arrives at its end.
QPointF arrivalDirection(const QPainterPath &path)
{
    const QPointF delta = path.pointAtPercent(1) - path.pointAtPercent(0.95);
    const qreal length = std::hypot(delta.x(), delta.y());
    return length > 0 ? delta / length : QPointF(0, 1);
}

void drawEnd(QPainter &painter, const SketchLink &link, const Ink &ink)
{
    const QPointF tip = link.path.pointAtPercent(1);
    const QPointF along = arrivalDirection(link.path);
    const QPointF across(-along.y(), along.x());
    switch (link.end) {
    case SketchLinkEnd::None:
        return;
    case SketchLinkEnd::Arrow: {
        QPainterPath head(tip);
        head.lineTo(tip - along * arrowLength + across * arrowHalfWidth);
        head.lineTo(tip - along * arrowLength - across * arrowHalfWidth);
        head.closeSubpath();
        painter.fillPath(head, ink.line);
        return;
    }
    case SketchLinkEnd::HollowTriangle: {
        QPainterPath head(tip);
        head.lineTo(tip - along * triangleLength + across * triangleHalfWidth);
        head.lineTo(tip - along * triangleLength - across * triangleHalfWidth);
        head.closeSubpath();
        painter.setPen(QPen(ink.line, outlineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(ink.surface);
        painter.drawPath(head);
        return;
    }
    case SketchLinkEnd::CrowFoot: {
        const QPointF root = tip - along * crowFootLength;
        painter.setPen(QPen(ink.line, outlineWidth, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(root, tip + across * crowFootHalfWidth);
        painter.drawLine(root, tip - across * crowFootHalfWidth);
        return;
    }
    }
}

void drawLabel(QPainter &painter, const QPainterPath &path, const Ink &ink)
{
    const QPointF middle = path.pointAtPercent(0.5);
    const QRectF bar(middle.x() - labelWidth / 2, middle.y() - 7 - textBarHeight, labelWidth, textBarHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink.surface);
    painter.drawRoundedRect(bar.adjusted(-3, -2, 3, 2), 3, 3);
    painter.setBrush(ink.text);
    painter.drawRoundedRect(bar, textBarHeight / 2, textBarHeight / 2);
}

} // namespace

void drawNode(QPainter &painter, const SketchNode &node, qreal progress, const Ink &ink)
{
    const qreal eased = easeOut(progress);
    const qreal scale = appearingScale + (1 - appearingScale) * eased;
    const QPointF center = node.rect.center();
    painter.save();
    painter.setOpacity(painter.opacity() * eased);
    painter.translate(center);
    painter.scale(scale, scale);
    painter.translate(-center);
    drawShape(painter, node, ink);
    painter.restore();
}

void drawLink(QPainter &painter, const SketchLink &link, qreal progress, const Ink &ink)
{
    painter.save();
    QPen pen(ink.line, outlineWidth, Qt::SolidLine, Qt::RoundCap);
    if (link.isDashed)
        pen.setDashPattern({4, 3});
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    if (progress >= 1) {
        painter.drawPath(link.path);
        drawEnd(painter, link, ink);
        if (link.hasLabel)
            drawLabel(painter, link.path, ink);
        painter.restore();
        return;
    }
    painter.drawPath(partOf(link.path, progress));
    // The pen, at the tip of what is drawn.
    const QPointF tip = link.path.pointAtPercent(progress);
    painter.setPen(Qt::NoPen);
    painter.setBrush(withAlpha(ink.accent, 0.25));
    painter.drawEllipse(tip, penRadius * 2.2, penRadius * 2.2);
    painter.setBrush(ink.accent);
    painter.drawEllipse(tip, penRadius, penRadius);
    painter.restore();
}

bool isBackdrop(SketchShape shape)
{
    return shape == SketchShape::Boundary || shape == SketchShape::Lifeline;
}

} // namespace SketchPainting
