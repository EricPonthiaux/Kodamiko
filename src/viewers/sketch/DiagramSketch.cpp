// SPDX-License-Identifier: GPL-3.0-only

#include "DiagramSketch.h"

#include "SketchPainting.h"
#include "SketchScenes.h"
#include "ui/Motion.h"
#include "ui/ThemeStyle.h"

#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {

constexpr int frameMs = 16;
// Each stroke takes this long, within these bounds for the whole loop.
constexpr qreal strokeMs = 320;
constexpr qreal shortestCycleMs = 5000;
constexpr qreal longestCycleMs = 8500;
// The strokes take the start of the loop; the full sketch then rests, and
// fades out at the end.
constexpr qreal drawingEnd = 0.78;
constexpr qreal fadeStart = 0.9;

constexpr QSize preferredSize{520, 280};
constexpr qreal cornerRadiusPx = 10;
constexpr qreal gridStepPx = 18;
constexpr qreal gridDotRadiusPx = 1;
constexpr qreal sceneMarginPx = 20;
// How much of the placeholder text's color the card takes.
constexpr qreal cardLift = 0.33;

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(alpha);
    return color;
}

void drawGrid(QPainter &painter, const QRectF &card, const QColor &dot)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(dot);
    const int columns = int(card.width() / gridStepPx);
    const int rows = int(card.height() / gridStepPx);
    for (int row = 1; row < rows; ++row) {
        for (int column = 1; column < columns; ++column)
            painter.drawEllipse(card.topLeft() + QPointF(column, row) * gridStepPx, gridDotRadiusPx, gridDotRadiusPx);
    }
}

// The card's color: AlternateBase lifted towards the placeholder text, so
// that the card stands out of the view's Base. Opaque, as what hides a link
// under its label must match it.
QColor cardColor(const QPalette &palette)
{
    return ThemeStyle::mixed(palette.color(QPalette::AlternateBase), palette.color(QPalette::PlaceholderText),
                             cardLift);
}

// Scales and centers the scene's space in `area`.
void fitScene(QPainter &painter, const QRectF &area)
{
    const qreal scale = std::min(area.width() / sketchSceneWidth, area.height() / sketchSceneHeight);
    painter.translate(area.center());
    painter.scale(scale, scale);
    painter.translate(-sketchSceneWidth / 2, -sketchSceneHeight / 2);
}

} // namespace

DiagramSketch::DiagramSketch(QStringView kindId, QWidget *parent)
    : QWidget(parent)
    , scene_(SketchScenes::forKind(kindId))
    , frame_(new QTimer(this))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    frame_->setInterval(frameMs);
    connect(frame_, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    connect(&Motion::policy(), &Motion::Policy::enabledChanged, this, [this] { updateMotion(); });
}

void DiagramSketch::setState(State state)
{
    state_ = state;
    clock_.start();
    updateMotion();
}

QSize DiagramSketch::sizeHint() const
{
    return preferredSize;
}

void DiagramSketch::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const QColor muted = palette().color(QPalette::PlaceholderText);
    QPainterPath cardShape;
    cardShape.addRoundedRect(card, cornerRadiusPx, cornerRadiusPx);
    painter.fillPath(cardShape, cardColor(palette()));
    painter.setClipPath(cardShape);
    drawGrid(painter, card, withAlpha(muted, 0.35));
    painter.setClipping(false);
    painter.setPen(QPen(withAlpha(muted, 0.25), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(cardShape);

    const qreal cycle = cycleProgress();
    if (cycle > fadeStart)
        painter.setOpacity(1 - (cycle - fadeStart) / (1 - fadeStart));
    fitScene(painter, card.adjusted(sceneMarginPx, sceneMarginPx, -sceneMarginPx, -sceneMarginPx));
    drawScene(painter, cycle);
}

// The frames and lifelines, the links that cross them, then the shapes the
// links join.
void DiagramSketch::drawScene(QPainter &painter, qreal cycle) const
{
    const QColor muted = palette().color(QPalette::PlaceholderText);
    const bool failed = state_ == State::Failed;
    const SketchPainting::Ink ink{
        .accent = failed ? muted : palette().color(QPalette::Highlight),
        .line = withAlpha(muted, failed ? 0.5 : 0.8),
        .text = withAlpha(muted, 0.45),
        .surface = cardColor(palette()),
    };
    const auto drawNodes = [&](bool backdrops) {
        for (qsizetype i = 0; i < scene_.strokes.size(); ++i) {
            const SketchStroke &stroke = scene_.strokes.at(i);
            if (stroke.kind != SketchStroke::Kind::Node)
                continue;
            const SketchNode &node = scene_.nodes.at(stroke.index);
            const qreal progress = strokeProgress(i, cycle);
            if (progress > 0 && SketchPainting::isBackdrop(node.shape) == backdrops)
                SketchPainting::drawNode(painter, node, progress, ink);
        }
    };
    drawNodes(true);
    for (qsizetype i = 0; i < scene_.strokes.size(); ++i) {
        const SketchStroke &stroke = scene_.strokes.at(i);
        const qreal progress = strokeProgress(i, cycle);
        if (stroke.kind == SketchStroke::Kind::Link && progress > 0)
            SketchPainting::drawLink(painter, scene_.links.at(stroke.index), progress, ink);
    }
    drawNodes(false);
}

void DiagramSketch::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateMotion();
}

void DiagramSketch::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    updateMotion();
}

// Frames only while they can be seen: a review or a diagram being drawn
// lasts minutes, often in a hidden tab or a minimized window.
void DiagramSketch::updateMotion()
{
    if (state_ == State::Drawing && Motion::isOnScreen(this) && Motion::policy().isEnabled())
        frame_->start();
    else
        frame_->stop();
    update();
}

qreal DiagramSketch::cycleProgress() const
{
    if (state_ != State::Drawing || !Motion::policy().isEnabled())
        return drawingEnd;
    const qreal cycleMs =
        std::clamp(qreal(scene_.strokes.size()) * strokeMs / drawingEnd, shortestCycleMs, longestCycleMs);
    return std::fmod(qreal(clock_.elapsed()), cycleMs) / cycleMs;
}

qreal DiagramSketch::strokeProgress(qsizetype index, qreal cycle) const
{
    const qreal duration = drawingEnd / qreal(std::max<qsizetype>(1, scene_.strokes.size()));
    return std::clamp((cycle - qreal(index) * duration) / duration, 0.0, 1.0);
}
