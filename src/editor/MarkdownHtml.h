// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QColor>
#include <QString>

class CodeColorizer;

// Markdown → HTML as GitHub renders it, for the preview: the same parser
// (cmark-gfm with GitHub's extensions: tables, strikethrough, autolinks, task
// lists, tag filter, footnotes) and the markup GitHub adds around it, which
// its style sheet expects: heading anchors with their link icon, code blocks
// highlighted with its "pl-" classes, task list classes, alerts
// ("> [!NOTE]"…).
namespace MarkdownHtml {

// Body of the document (what goes inside <article class="markdown-body">).
// Headings carry their source line (1-based) in a "data-line" attribute.
// `colorizer` highlights the code blocks; nullptr leaves them plain.
QString body(const QString &markdown, CodeColorizer *colorizer);

// Whole page: GitHub's light style sheet and its layout, `body` in the
// article. Page scripts are forbidden. The preview looks like GitHub
// whatever the editor theme.
QString page(const QString &body);

// GitHub's light page color: the page's background.
QColor pageColor();

// "My Title!" → "my-title", as GitHub names heading anchors.
QString headingAnchor(const QString &heading);

// GitHub's "pl-" class for a syntax category of the editor theme
// ("keyword" → "pl-k"); empty for text GitHub leaves plain.
QString prettyLightsClass(const QString &category, const QString &languageId);

} // namespace MarkdownHtml
