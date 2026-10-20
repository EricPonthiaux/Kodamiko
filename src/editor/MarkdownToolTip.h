// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

class CodeColorizer;
struct Theme;

// Markdown as QToolTip shows it (rich text, a subset of HTML), in the
// editor's theme: code blocks highlighted as in the editor, headings and
// links in the accent color, rules in the borders' color. The background
// stays the tooltip's throughout.
namespace MarkdownToolTip {

struct Style {
    const Theme &theme;
    // Highlights the code blocks; nullptr leaves them in the text color.
    // Not owned.
    CodeColorizer *colorizer = nullptr;
    // Language of the code blocks whose fence names none.
    QString languageId;
};

// `markdown` (CommonMark with GitHub's tables, task lists and
// strikethrough) as tooltip HTML, in the tooltip's font.
QString html(const QString &markdown, const Style &style);

} // namespace MarkdownToolTip
