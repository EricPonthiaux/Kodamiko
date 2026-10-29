// SPDX-License-Identifier: GPL-3.0-only

#include "CleanDiffState.h"

#include "ui/AppFonts.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <array>
#include <utility>

namespace {

constexpr QSizeF artSize{440, 280};
constexpr qreal inset = 24;
constexpr qreal captionGap = 8;
constexpr qreal artGap = 16;
constexpr qreal contentWidth = 440;
constexpr int textFlags = Qt::AlignHCenter | Qt::TextWordWrap;

QColor translucent(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

void paintBackdrop(QPainter &painter, const Theme &theme)
{
    const QPointF centre(220, 140);
    QRadialGradient glow(centre, 185);
    glow.setColorAt(0, translucent(theme.accent, 30));
    glow.setColorAt(1, translucent(theme.accent, 0));
    painter.fillRect(QRectF(QPointF(), artSize), glow);

    painter.save();
    painter.translate(centre);
    painter.rotate(-14);
    painter.setPen(QPen(translucent(theme.accent, 35), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(), 192, 104);
    painter.restore();

    constexpr std::array<QPointF, 6> dots{{{54, 126}, {84, 207}, {155, 28}, {337, 38}, {386, 155}, {352, 232}}};
    painter.setPen(Qt::NoPen);
    painter.setBrush(translucent(theme.accent, 100));
    for (const QPointF &dot : dots)
        painter.drawEllipse(dot, 2, 2);
    painter.setPen(QPen(translucent(theme.accent, 110), 1.5, Qt::SolidLine, Qt::RoundCap));
    for (const QPointF &spark : {QPointF(70, 65), QPointF(373, 89)}) {
        painter.drawLine(spark - QPointF(4, 0), spark + QPointF(4, 0));
        painter.drawLine(spark - QPointF(0, 4), spark + QPointF(0, 4));
    }
}

void paintCode(QPainter &painter, const Theme &theme)
{
    // The same quiet lines on both sheets: there is no difference left.
    constexpr std::array<QLineF, 7> lines{{{19, 51, 46, 51},
                                           {53, 51, 88, 51},
                                           {29, 68, 70, 68},
                                           {77, 68, 103, 68},
                                           {29, 85, 89, 85},
                                           {29, 102, 71, 102},
                                           {19, 119, 43, 119}}};
    painter.setPen(QPen(translucent(theme.mutedText, 95), 4, Qt::SolidLine, Qt::RoundCap));
    for (const QLineF &line : lines)
        painter.drawLine(line);
    painter.setPen(QPen(translucent(theme.accent, 180), 4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(lines[0]);
    painter.drawLine(lines[3]);
    painter.drawLine(lines[5]);
}

void paintSheet(QPainter &painter, const QPointF &position, qreal angle, const Theme &theme)
{
    constexpr QRectF sheet(0, 0, 128, 148);
    painter.save();
    painter.translate(position);
    painter.rotate(angle);
    painter.setPen(Qt::NoPen);
    painter.setBrush(translucent(theme.accent, 10));
    painter.drawRoundedRect(sheet.adjusted(-5, 3, 5, 11), 14, 14);
    painter.setBrush(translucent(theme.accent, 16));
    painter.drawRoundedRect(sheet.translated(0, 5), 10, 10);

    QLinearGradient paper(sheet.topLeft(), sheet.bottomRight());
    paper.setColorAt(0, theme.window);
    paper.setColorAt(1, theme.background);
    painter.setBrush(paper);
    painter.setPen(QPen(translucent(theme.mutedText, 85), 1.2));
    painter.drawRoundedRect(sheet, 10, 10);
    painter.setPen(QPen(translucent(theme.mutedText, 45), 1));
    painter.drawLine(QPointF(0, 30), QPointF(128, 30));
    painter.setPen(Qt::NoPen);
    painter.setBrush(translucent(theme.accent, 140));
    for (const int x : {18, 28, 38})
        painter.drawEllipse(QPointF(x, 15), 2, 2);
    paintCode(painter, theme);
    painter.restore();
}

void paintBranch(QPainter &painter, const Theme &theme)
{
    QPainterPath branch;
    branch.moveTo(158, 179);
    branch.lineTo(158, 201);
    branch.cubicTo(158, 225, 220, 208, 220, 239);
    branch.moveTo(290, 167);
    branch.lineTo(290, 192);
    branch.cubicTo(290, 225, 220, 208, 220, 239);
    painter.setPen(QPen(translucent(theme.accent, 150), 2, Qt::SolidLine, Qt::RoundCap));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(branch);
    painter.setBrush(theme.background);
    for (const QPointF &node : {QPointF(158, 197), QPointF(290, 185)})
        painter.drawEllipse(node, 4, 4);
}

void paintCheck(QPainter &painter, const Theme &theme)
{
    const QPointF centre(220, 236);
    painter.setPen(Qt::NoPen);
    painter.setBrush(translucent(theme.accent, 12));
    painter.drawEllipse(centre, 31, 31);
    painter.setBrush(theme.background);
    painter.drawEllipse(centre, 23, 23);
    painter.setBrush(translucent(theme.accent, 24));
    painter.setPen(QPen(translucent(theme.accent, 175), 1.5));
    painter.drawEllipse(centre, 23, 23);

    QPainterPath check;
    check.moveTo(210, 236);
    check.lineTo(217, 243);
    check.lineTo(231, 229);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(theme.accent, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(check);
}

void paintIllustration(QPainter &painter, const QRectF &area, const Theme &theme)
{
    if (area.height() <= 0)
        return;
    painter.save();
    const qreal scale = area.height() / artSize.height();
    painter.translate(area.center().x() - artSize.width() * scale / 2, area.top());
    painter.scale(scale, scale);
    paintBackdrop(painter, theme);
    paintBranch(painter, theme);
    paintSheet(painter, QPointF(102, 63), -9, theme);
    paintSheet(painter, QPointF(224, 39), 8, theme);
    paintCheck(painter, theme);
    painter.restore();
}

} // namespace

CleanDiffState::CleanDiffState(Theme theme, QWidget *parent)
    : QWidget(parent)
    , theme_(std::move(theme))
{
    setContextMenuPolicy(Qt::PreventContextMenu);
    setAccessibleName(tr("No pending changes"));
    setAccessibleDescription(tr("Your next changes will appear here."));
    AppFonts::setInterfaceFont(this, 1, QFont::Normal);
    AppFonts::followApplicationFont(this, [this] { update(); });
}

void CleanDiffState::applyTheme(const Theme &theme)
{
    theme_ = theme;
    update();
}

QSize CleanDiffState::sizeHint() const
{
    return {488, 380};
}

void CleanDiffState::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), theme_.background);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal width = std::max(1.0, std::min(contentWidth, this->width() - inset * 2));
    QFont titleFont = font();
    titleFont.setPointSizeF(font().pointSizeF() * 1.15);
    titleFont.setWeight(QFont::DemiBold);
    const QRectF textArea(0, 0, width, 1000);
    const qreal titleHeight = QFontMetricsF(titleFont).boundingRect(textArea, textFlags, accessibleName()).height();
    const qreal captionHeight =
        QFontMetricsF(font()).boundingRect(textArea, textFlags, accessibleDescription()).height();
    const qreal textHeight = titleHeight + captionGap + captionHeight;
    const qreal artHeight =
        std::clamp(height() - textHeight - artGap - inset * 2, 0.0, artSize.height() * width / artSize.width());
    const qreal totalHeight = artHeight + artGap + textHeight;
    const qreal top = std::max(inset, (height() - totalHeight) * 0.44);
    const qreal left = (this->width() - width) / 2;
    paintIllustration(painter, QRectF(left, top, width, artHeight), theme_);
    const qreal titleTop = top + artHeight + artGap;
    painter.setFont(titleFont);
    painter.setPen(theme_.foreground);
    painter.drawText(QRectF(left, titleTop, width, titleHeight), textFlags, accessibleName());
    painter.setFont(font());
    painter.setPen(theme_.mutedText);
    painter.drawText(QRectF(left, titleTop + titleHeight + captionGap, width, captionHeight), textFlags,
                     accessibleDescription());
}
