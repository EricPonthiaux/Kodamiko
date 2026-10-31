// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewSketch.h"

#include "ReviewText.h"
#include "ui/Motion.h"

#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace {

enum class Mark {
    None,
    Warning,
    Error
};

// A blank line of code: its indentation, the share of the rest of the
// page its text takes, and the finding the reviewer marks on it.
struct SketchLine {
    int indent = 0;
    qreal length = 0;
    Mark mark = Mark::None;
};

// The colors of the sketch: of the reading band, of the blank text, and of
// the two kinds of findings.
struct Ink {
    QColor accent;
    QColor text;
    QColor warning;
    QColor error;
};

// Where the lines are: the page inside the card, a line's height, and how
// many lines the band has read, fractions included.
struct Reading {
    QRectF area;
    qreal lineHeight = 0;
    qreal linesRead = 0;
};

// The finding of a line: a dot in the gutter at `dot`, and a wavy underline
// under the line's `bar`.
struct FoundMark {
    QRectF bar;
    QPointF dot;
    QColor color;
};

constexpr std::array<SketchLine, 12> page{{
    {.indent = 0, .length = 0.55},
    {.indent = 0, .length = 0.08},
    {.indent = 1, .length = 0.62},
    {.indent = 1, .length = 0.48, .mark = Mark::Warning},
    {.indent = 2, .length = 0.70},
    {.indent = 2, .length = 0.36},
    {.indent = 1, .length = 0.10},
    {.indent = 1, .length = 0.58, .mark = Mark::Error},
    {.indent = 2, .length = 0.44},
    {.indent = 1, .length = 0.66},
    {.indent = 1, .length = 0.30, .mark = Mark::Warning},
    {.indent = 0, .length = 0.06},
}};

constexpr int frameMs = 16;
constexpr qreal cycleMs = 7000;
// The band reads the lines at the start of the loop; the page read then
// rests, and fades out at the end.
constexpr qreal readingEnd = 0.75;
constexpr qreal fadeStart = 0.9;
// A finding's mark grows while the band reads this many lines past it.
constexpr qreal markGrowthLines = 0.8;

constexpr QSize preferredSize{520, 280};
constexpr qreal cornerRadiusPx = 10;
constexpr qreal pageMarginPx = 22;
constexpr qreal gutterWidthPx = 34;
constexpr qreal indentPx = 22;
constexpr qreal textBarHeightPx = 4;
constexpr qreal markRadiusPx = 4;
constexpr qreal waveAmplitudePx = 1.6;
constexpr qreal waveLengthPx = 7;
constexpr qreal caretWidthPx = 2;
// The caret's gap to the band's top and bottom.
constexpr qreal caretInsetPx = 3;
// How far the band reaches into the gutter, left of the text.
constexpr qreal bandOverhangPx = 8;
constexpr qreal bandRadiusPx = 4;
// A line number's bar: its gap to the page's left, its share of the gutter.
constexpr qreal numberInsetPx = 4;
constexpr qreal numberWidthShare = 0.35;
// Between a finding's dot and the gutter's right edge.
constexpr qreal markGapPx = 6;
// Between a line's text and its wavy underline.
constexpr qreal waveGapPx = 2;

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(alpha);
    return color;
}

qreal easeOut(qreal t)
{
    return 1 - std::pow(1 - t, 3);
}

void drawCard(QPainter &painter, const QRectF &card, const QPalette &palette)
{
    QPainterPath shape;
    shape.addRoundedRect(card, cornerRadiusPx, cornerRadiusPx);
    painter.fillPath(shape, palette.color(QPalette::Base));
    painter.setPen(QPen(withAlpha(palette.color(QPalette::PlaceholderText), 0.25), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(shape);
}

// The text bar of line `index`, centered on its line.
QRectF textBar(const Reading &reading, qsizetype index)
{
    const SketchLine &line = page.at(index);
    const qreal left = reading.area.left() + gutterWidthPx + line.indent * indentPx;
    const qreal centerY = reading.area.top() + reading.lineHeight * (qreal(index) + 0.5);
    const qreal width = (reading.area.right() - left) * line.length;
    return {left, centerY - textBarHeightPx / 2, width, textBarHeightPx};
}

void drawLines(QPainter &painter, const Reading &reading, const Ink &ink)
{
    painter.setPen(Qt::NoPen);
    for (qsizetype i = 0; i < qsizetype(page.size()); ++i) {
        const QRectF bar = textBar(reading, i);
        // The lines brighten as the band passes them.
        const qreal read = std::clamp(reading.linesRead - qreal(i), 0.0, 1.0);
        painter.setBrush(withAlpha(ink.text, 0.25 + 0.3 * read));
        painter.drawRoundedRect(bar, textBarHeightPx / 2, textBarHeightPx / 2);
        // The line number in the gutter.
        const QRectF number(reading.area.left() + numberInsetPx, bar.top(), gutterWidthPx * numberWidthShare,
                            textBarHeightPx);
        painter.setBrush(withAlpha(ink.text, 0.2));
        painter.drawRoundedRect(number, textBarHeightPx / 2, textBarHeightPx / 2);
    }
}

// A wavy underline under `bar`, the first `share` of its width.
QPainterPath wave(const QRectF &bar, qreal share)
{
    const qreal y = bar.bottom() + waveAmplitudePx + waveGapPx;
    const int length = int(bar.width() * share);
    QPainterPath path(QPointF(bar.left(), y));
    for (int x = 1; x <= length; ++x) {
        const qreal phase = x / waveLengthPx * 2 * std::numbers::pi;
        path.lineTo(bar.left() + x, y + waveAmplitudePx * std::sin(phase));
    }
    return path;
}

// `mark` grown to `progress` (0 to 1), the underline drawn from the left.
void drawMark(QPainter &painter, const FoundMark &mark, qreal progress)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(mark.color);
    painter.drawEllipse(mark.dot, markRadiusPx * progress, markRadiusPx * progress);
    painter.setPen(QPen(mark.color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(wave(mark.bar, progress));
}

void drawMarks(QPainter &painter, const Reading &reading, const Ink &ink)
{
    const qreal dotX = reading.area.left() + gutterWidthPx - markRadiusPx - markGapPx;
    for (qsizetype i = 0; i < qsizetype(page.size()); ++i) {
        const Mark mark = page.at(i).mark;
        // A finding appears once the band has left its line.
        const qreal grown = std::clamp((reading.linesRead - qreal(i) - 1) / markGrowthLines, 0.0, 1.0);
        if (mark == Mark::None || grown <= 0)
            continue;
        const QRectF bar = textBar(reading, i);
        drawMark(painter,
                 {.bar = bar,
                  .dot = QPointF(dotX, bar.center().y()),
                  .color = mark == Mark::Error ? ink.error : ink.warning},
                 easeOut(grown));
    }
}

// The band over the line being read, with a caret at its left.
void drawBand(QPainter &painter, const Reading &reading, const Ink &ink)
{
    const qreal lastCenter = qreal(page.size()) - 0.5;
    const qreal centerY = reading.area.top() + reading.lineHeight * std::clamp(reading.linesRead, 0.5, lastCenter);
    const QRectF band(reading.area.left() + gutterWidthPx - bandOverhangPx, centerY - reading.lineHeight / 2,
                      reading.area.width() - gutterWidthPx + bandOverhangPx, reading.lineHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(withAlpha(ink.accent, 0.12));
    painter.drawRoundedRect(band, bandRadiusPx, bandRadiusPx);
    painter.setBrush(ink.accent);
    painter.drawRect(QRectF(band.left(), band.top() + caretInsetPx, caretWidthPx, band.height() - 2 * caretInsetPx));
}

} // namespace

ReviewSketch::ReviewSketch(QWidget *parent)
    : QWidget(parent)
    , frame_(new QTimer(this))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    frame_->setInterval(frameMs);
    connect(frame_, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    connect(&Motion::policy(), &Motion::Policy::enabledChanged, this, [this] { updateMotion(); });
}

void ReviewSketch::setState(State state)
{
    state_ = state;
    clock_.start();
    updateMotion();
}

QSize ReviewSketch::sizeHint() const
{
    return preferredSize;
}

void ReviewSketch::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    drawCard(painter, card, palette());

    const QColor muted = palette().color(QPalette::PlaceholderText);
    const bool failed = state_ == State::Failed;
    const Ink ink{
        .accent = palette().color(QPalette::Highlight),
        .text = muted,
        .warning = failed ? muted : QColor(ReviewText::warningColor(palette())),
        .error = failed ? muted : QColor(ReviewText::errorColor(palette())),
    };
    const qreal cycle = cycleProgress();
    const QRectF area = card.adjusted(pageMarginPx, pageMarginPx, -pageMarginPx, -pageMarginPx);
    const Reading reading{
        .area = area,
        .lineHeight = area.height() / qreal(page.size()),
        .linesRead = std::min(cycle / readingEnd, 1.0) * qreal(page.size()),
    };
    if (cycle > fadeStart)
        painter.setOpacity(1 - (cycle - fadeStart) / (1 - fadeStart));
    if (cycle < readingEnd)
        drawBand(painter, reading, ink);
    drawLines(painter, reading, ink);
    drawMarks(painter, reading, ink);
}

void ReviewSketch::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateMotion();
}

void ReviewSketch::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    updateMotion();
}

// Frames only while they can be seen: a review or a diagram being drawn
// lasts minutes, often in a hidden tab or a minimized window.
void ReviewSketch::updateMotion()
{
    if (state_ == State::Reading && Motion::isOnScreen(this) && Motion::policy().isEnabled())
        frame_->start();
    else
        frame_->stop();
    update();
}

qreal ReviewSketch::cycleProgress() const
{
    if (state_ != State::Reading || !Motion::policy().isEnabled())
        return readingEnd;
    return std::fmod(qreal(clock_.elapsed()), cycleMs) / cycleMs;
}
