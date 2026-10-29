// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewExcerpt.h"

#include "ReviewText.h"
#include "ui/AppFonts.h"
#include "ui/ThemeStyle.h"

#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

// The lines shown before and after the finding's.
constexpr int contextLines = 2;
constexpr int tabWidth = 4;
constexpr qreal fontScale = 0.92;
constexpr int paddingPx = 10;
// Between two lines, over the font's line spacing.
constexpr int lineGapPx = 2;
// The width it asks for, in characters of the code font.
constexpr int preferredWidthChars = 60;
constexpr int numberGapPx = 12;
constexpr qreal cornerRadiusPx = 4;
constexpr qreal waveAmplitudePx = 1.5;
constexpr qreal waveLengthPx = 6;
// Between the wave and the bottom of its line.
constexpr qreal waveBottomGapPx = 1;

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(alpha);
    return color;
}

// A wavy underline from `left`, `length` pixels long, `y` its middle.
QPainterPath wave(qreal left, int length, qreal y)
{
    QPainterPath path(QPointF(left, y));
    for (int x = 1; x <= length; ++x) {
        const qreal phase = x / waveLengthPx * 2 * std::numbers::pi;
        path.lineTo(left + x, y + waveAmplitudePx * std::sin(phase));
    }
    return path;
}

} // namespace

ReviewExcerpt::ReviewExcerpt(const ReviewReport::Finding &finding, QWidget *parent)
    : QWidget(parent)
    , findingLine_(finding.line)
    , severity_(finding.severity)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFont(AppFonts::codeFont(font().pointSizeF() * fontScale));
    read(finding.path);
}

void ReviewExcerpt::read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QTextStream stream(&file);
    const int first = std::max(1, findingLine_ - contextLines);
    const int last = findingLine_ + contextLines;
    for (int number = 1; number <= last && !stream.atEnd(); ++number) {
        const QString text = stream.readLine();
        if (number >= first)
            lines_.append({.number = number, .text = QString(text).replace(u'\t', QString(tabWidth, u' '))});
    }
}

int ReviewExcerpt::lineHeight() const
{
    return fontMetrics().lineSpacing() + lineGapPx;
}

QSize ReviewExcerpt::sizeHint() const
{
    if (lines_.isEmpty())
        return {0, 0};
    return {fontMetrics().averageCharWidth() * preferredWidthChars, int(lines_.size()) * lineHeight() + 2 * paddingPx};
}

QSize ReviewExcerpt::minimumSizeHint() const
{
    return {0, sizeHint().height()};
}

void ReviewExcerpt::paintEvent(QPaintEvent *)
{
    if (lines_.isEmpty())
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath background;
    background.addRoundedRect(QRectF(rect()), cornerRadiusPx, cornerRadiusPx);
    painter.fillPath(background, palette().color(QPalette::Base));
    painter.setClipPath(background);

    const QFontMetrics metrics = fontMetrics();
    const QColor severity(severity_ == Diagnostic::Severity::Error ? ReviewText::errorColor(palette())
                                                                   : ReviewText::warningColor(palette()));
    const int numberWidth = metrics.horizontalAdvance(QString::number(lines_.last().number)) + 2 * numberGapPx;
    const int textWidth = width() - numberWidth - paddingPx;
    for (qsizetype i = 0; i < lines_.size(); ++i) {
        const Line &line = lines_.at(i);
        const QRect row(0, paddingPx + int(i) * lineHeight(), width(), lineHeight());
        const bool isFinding = line.number == findingLine_;
        if (isFinding)
            painter.fillRect(row, withAlpha(severity, 0.12));
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(QRect(row.left(), row.top(), numberWidth - numberGapPx, row.height()),
                         Qt::AlignRight | Qt::AlignVCenter, QString::number(line.number));
        const QString text = metrics.elidedText(line.text, Qt::ElideRight, textWidth);
        const QColor context =
            ThemeStyle::mixed(palette().color(QPalette::Base), palette().color(QPalette::Text), 0.72);
        painter.setPen(isFinding ? palette().color(QPalette::Text) : context);
        painter.drawText(row.adjusted(numberWidth, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, text);
        if (!isFinding)
            continue;
        // Under the code, not under the indentation.
        const qsizetype indent = std::ranges::find_if_not(text, [](QChar c) { return c.isSpace(); }) - text.begin();
        const int indentWidth = metrics.horizontalAdvance(text.left(indent));
        const int codeWidth = metrics.horizontalAdvance(text) - indentWidth;
        painter.setPen(QPen(severity, 1));
        painter.drawPath(wave(numberWidth + indentWidth, codeWidth, row.bottom() - waveAmplitudePx - waveBottomGapPx));
    }
}
