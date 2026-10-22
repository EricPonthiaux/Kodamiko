// SPDX-License-Identifier: GPL-3.0-only

#pragma once

class QLabel;
class QString;

// Text that may take several lines, in a label: wrapped at the label's
// width, its lines a little further apart than the font's own spacing,
// which reads tight past one line. Qt's style sheets have no line height
// for a label, so the text becomes rich text: the label's text() is then
// HTML.
namespace Paragraph {

// Shows plain `text` in `label`, wrapped and spaced; its line breaks are
// kept. Empty, it clears the label.
void setText(QLabel *label, const QString &text);

// Same, for `html` already in rich text (a link, a color).
void setHtml(QLabel *label, const QString &html);

} // namespace Paragraph
