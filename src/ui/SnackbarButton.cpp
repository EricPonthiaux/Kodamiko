// SPDX-License-Identifier: GPL-3.0-only

#include "SnackbarButton.h"

#include <QStyleOptionButton>
#include <QStylePainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
#include <cmath>

namespace {

constexpr int minimumWidthPx = 96;
constexpr int minimumHeightPx = 34;

void prepareText(QTextDocument &document, const QPushButton &button, int width)
{
    document.setDocumentMargin(0);
    document.setDefaultFont(button.font());
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(Qt::AlignHCenter);
    document.setDefaultTextOption(option);
    // Match QPushButton's mnemonic syntax, including escaped ampersands.
    QString label;
    const QString text = button.text();
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('&') && i + 1 < text.size())
            ++i;
        label += text.at(i);
    }
    document.setPlainText(label);
    document.setTextWidth(std::max(1, width));
}

} // namespace

SnackbarButton::SnackbarButton(const QString &text, QWidget *parent)
    : QPushButton(text, parent)
{
    setFocusPolicy(Qt::TabFocus);
    setAutoDefault(false);
}

QSize SnackbarButton::sizeHint() const
{
    ensurePolished();
    QTextDocument document;
    prepareText(document, *this, QWIDGETSIZE_MAX);
    const QSize textSize(int(std::ceil(document.idealWidth())) + 2, int(std::ceil(document.size().height())));
    return (textSize + textPadding()).expandedTo(QSize(minimumWidthPx, minimumHeightPx));
}

QSize SnackbarButton::textPadding() const
{
    // Include the active style's border and padding, also under a style sheet.
    const QSize natural = QPushButton::sizeHint();
    return {std::max(28, natural.width() - fontMetrics().horizontalAdvance(text())),
            std::max(12, natural.height() - fontMetrics().height())};
}

int SnackbarButton::heightForWidth(int width) const
{
    ensurePolished();
    const QSize padding = textPadding();
    QTextDocument document;
    prepareText(document, *this, width - padding.width());
    return std::max(minimumHeightPx, int(std::ceil(document.size().height())) + padding.height());
}

void SnackbarButton::paintEvent(QPaintEvent *)
{
    QTextDocument document;
    prepareText(document, *this, width() - textPadding().width());
    // Force layout before taking its visual lines. The style paints the label
    // itself so primary, disabled, pressed and focus colors remain native.
    document.size();
    QStringList lines;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        const QTextLayout *layout = block.layout();
        for (int i = 0; i < layout->lineCount(); ++i) {
            const QTextLine line = layout->lineAt(i);
            lines.append(block.text().mid(line.textStart(), line.textLength()));
        }
    }
    QStyleOptionButton option;
    initStyleOption(&option);
    option.text = lines.join(QLatin1Char('\n')).replace(QLatin1Char('&'), QStringLiteral("&&"));
    QStylePainter painter(this);
    painter.drawControl(QStyle::CE_PushButton, option);
}
