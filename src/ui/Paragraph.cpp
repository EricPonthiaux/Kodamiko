// SPDX-License-Identifier: GPL-3.0-only

#include "Paragraph.h"

#include <QLabel>
#include <QString>

namespace {

// Of the font's line spacing; it also adds this share under the last line.
constexpr int lineHeightPercent = 120;

} // namespace

namespace Paragraph {

void setText(QLabel *label, const QString &text)
{
    setHtml(label, text.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br>")));
}

void setHtml(QLabel *label, const QString &html)
{
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    if (html.isEmpty()) {
        label->clear();
        return;
    }
    label->setText(QStringLiteral("<div style=\"line-height:%1%\">%2</div>").arg(lineHeightPercent).arg(html));
}

} // namespace Paragraph
