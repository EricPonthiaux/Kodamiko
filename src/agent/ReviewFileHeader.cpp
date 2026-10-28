// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewFileHeader.h"

#include "ReviewText.h"
#include "ui/AppFonts.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {

constexpr int caretAreaPx = 18;
constexpr qreal caretSizePx = 3.5;
constexpr qreal dotRadiusPx = 3.5;
constexpr int countGapPx = 12;
// Between a count's dot and its number.
constexpr qreal dotGapPx = 4;
constexpr int verticalPaddingPx = 5;
constexpr qreal cornerRadiusPx = 4;
constexpr qreal hoveredAlpha = 0.05;
constexpr qreal fontScale = 0.95;

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(alpha);
    return color;
}

} // namespace

ReviewFileHeader::ReviewFileHeader(const QString &relativePath, Counts counts, QWidget *parent)
    : QWidget(parent)
    , counts_(counts)
{
    const qsizetype slash = relativePath.lastIndexOf(u'/');
    folder_ = relativePath.left(slash + 1);
    fileName_ = relativePath.mid(slash + 1);
    setFont(AppFonts::codeFont(font().pointSizeF() * fontScale));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Fold or unfold the findings of %1").arg(fileName_));
}

void ReviewFileHeader::setExpanded(bool expanded)
{
    isExpanded_ = expanded;
    update();
}

QSize ReviewFileHeader::sizeHint() const
{
    return {caretAreaPx + fontMetrics().horizontalAdvance(folder_ + fileName_),
            fontMetrics().height() + 2 * verticalPaddingPx};
}

void ReviewFileHeader::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF area(rect());
    if (isHovered_) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(withAlpha(palette().color(QPalette::Text), hoveredAlpha));
        painter.drawRoundedRect(area, cornerRadiusPx, cornerRadiusPx);
    }
    drawCaret(painter, QPointF(caretAreaPx / 2.0, area.center().y()));
    const qreal countsLeft = drawCounts(painter, area.adjusted(0, 0, -countGapPx / 2.0, 0));

    // The name stays whole; the folder is elided from its start.
    const QFontMetricsF metrics(font());
    const qreal nameWidth = metrics.horizontalAdvance(fileName_);
    const qreal folderWidth = std::max(0.0, countsLeft - countGapPx - caretAreaPx - nameWidth);
    const QString folder = metrics.elidedText(folder_, Qt::ElideLeft, folderWidth);
    const QRectF text(caretAreaPx, area.top(), area.width() - caretAreaPx, area.height());
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, folder);
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(text.adjusted(metrics.horizontalAdvance(folder), 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                     fileName_);
}

void ReviewFileHeader::drawCaret(QPainter &painter, QPointF center) const
{
    QPainterPath caret;
    if (isExpanded_) {
        caret.moveTo(center + QPointF(-caretSizePx, -caretSizePx / 2));
        caret.lineTo(center + QPointF(caretSizePx, -caretSizePx / 2));
        caret.lineTo(center + QPointF(0, caretSizePx));
    } else {
        caret.moveTo(center + QPointF(-caretSizePx / 2, -caretSizePx));
        caret.lineTo(center + QPointF(caretSizePx, 0));
        caret.lineTo(center + QPointF(-caretSizePx / 2, caretSizePx));
    }
    caret.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().color(QPalette::PlaceholderText));
    painter.drawPath(caret);
}

qreal ReviewFileHeader::drawCounts(QPainter &painter, const QRectF &area) const
{
    const QFontMetricsF metrics(font());
    qreal right = area.right();
    const auto drawCount = [&](qsizetype count, const QString &color) {
        if (count == 0)
            return;
        const QString text = QString::number(count);
        const qreal textWidth = metrics.horizontalAdvance(text);
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(QRectF(right - textWidth, area.top(), textWidth, area.height()), Qt::AlignVCenter, text);
        right -= textWidth + dotGapPx + dotRadiusPx;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(color));
        painter.drawEllipse(QPointF(right, area.center().y()), dotRadiusPx, dotRadiusPx);
        right -= dotRadiusPx + countGapPx;
    };
    // The warnings at the right edge, the errors before them.
    drawCount(counts_.warnings, ReviewText::warningColor(palette()));
    drawCount(counts_.errors, ReviewText::errorColor(palette()));
    return right;
}

void ReviewFileHeader::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit toggled();
}

void ReviewFileHeader::enterEvent(QEnterEvent *event)
{
    isHovered_ = true;
    update();
    QWidget::enterEvent(event);
}

void ReviewFileHeader::leaveEvent(QEvent *event)
{
    isHovered_ = false;
    update();
    QWidget::leaveEvent(event);
}
