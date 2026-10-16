#pragma once

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
// `colorizer` highlights the code blocks; nullptr leaves them plain.
QString body(const QString &markdown, CodeColorizer *colorizer);

// Whole page: GitHub's style sheet (dark or light) and its layout,
// `body` in the article. Page scripts are forbidden.
QString page(const QString &body, bool dark);

// GitHub's page color, dark or light: the page's background.
QString pageColor(bool dark);

// "My Title!" → "my-title", as GitHub names heading anchors.
QString headingAnchor(const QString &heading);

// GitHub's "pl-" class for a syntax category of the editor theme
// ("keyword" → "pl-k"); empty for text GitHub leaves plain.
QString prettyLightsClass(const QString &category, const QString &languageId);

} // namespace MarkdownHtml
