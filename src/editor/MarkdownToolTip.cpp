// SPDX-License-Identifier: GPL-3.0-only

#include "MarkdownToolTip.h"

#include "CodeColorizer.h"
#include "ui/Theme.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolTip>

#include <optional>

namespace MarkdownToolTip {
namespace {

// Room above and below a rule ("---").
constexpr int ruleSpacingPx = 6;

// A line of a fenced code block: Qt's Markdown reader makes one block per
// line, its lines unbreakable.
bool isCodeLine(const QTextBlock &block)
{
    return block.blockFormat().nonBreakableLines();
}

QString fenceLanguage(const QTextBlock &block)
{
    return block.blockFormat().property(QTextFormat::BlockCodeLanguage).toString();
}

// The theme's style for a syntax category, as a character format.
std::optional<QTextCharFormat> syntaxFormat(const Theme &theme, const QString &category)
{
    const int index = theme.syntaxStyleIndex(category);
    if (index < 0)
        return std::nullopt;
    const TextStyle &style = theme.syntaxStyles.at(index).second;
    QTextCharFormat format;
    format.setForeground(style.foreground);
    if (style.bold)
        format.setFontWeight(QFont::Bold);
    if (style.italic)
        format.setFontItalic(true);
    return format;
}

// The code block starting at `first`, highlighted as in the editor. Its
// background stays the tooltip's: the editor's would make a patch of
// another color. Returns its last line.
QTextBlock styleCodeBlock(const QTextBlock &first, const Style &style)
{
    QTextBlock last = first;
    QStringList lines{first.text()};
    while (isCodeLine(last.next()) && fenceLanguage(last.next()) == fenceLanguage(first)) {
        last = last.next();
        lines.append(last.text());
    }
    if (!style.colorizer)
        return last;

    QTextCursor cursor(first);
    const QString fence = fenceLanguage(first);
    const QString languageId = fence.isEmpty() ? style.languageId : CodeColorizer::languageOfFence(fence);
    // The lines joined by "\n": the document's positions, from the block's.
    for (const CodeColorizer::Span &span : style.colorizer->categorize(lines.join(u'\n'), languageId)) {
        const std::optional<QTextCharFormat> format = syntaxFormat(style.theme, span.category);
        if (!format)
            continue;
        cursor.setPosition(first.position() + span.start);
        cursor.setPosition(first.position() + span.start + span.length, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(*format);
    }
    return last;
}

// Links in the accent color.
void styleLinks(const QTextBlock &block, const Theme &theme)
{
    QTextCharFormat link;
    link.setForeground(theme.accent);
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.charFormat().isAnchor())
            continue;
        QTextCursor cursor(block);
        cursor.setPosition(fragment.position());
        cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(link);
    }
}

// A rule ("---") in the borders' color. Qt draws a real rule in the
// palette's color whatever the format says: it becomes an empty line,
// 1 pixel high, filled with the color.
void styleRule(const QTextBlock &block, const Theme &theme)
{
    QTextBlockFormat rule = block.blockFormat();
    rule.clearProperty(QTextFormat::BlockTrailingHorizontalRulerWidth);
    rule.setBackground(theme.border);
    // The same room on both sides: the next block (a code block has none
    // of its own) would otherwise touch the line.
    rule.setTopMargin(ruleSpacingPx);
    rule.setBottomMargin(ruleSpacingPx);
    QTextCursor cursor(block);
    cursor.setBlockFormat(rule);
    QFont onePixel;
    onePixel.setPixelSize(1);
    QTextCharFormat height;
    height.setFont(onePixel, QTextCharFormat::FontPropertiesSpecifiedOnly);
    cursor.setBlockCharFormat(height);
}

// Headings in the accent color, rules in the borders' color.
void styleTextBlock(const QTextBlock &block, const Theme &theme)
{
    if (block.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth)) {
        styleRule(block, theme);
        return;
    }
    QTextCursor cursor(block);
    if (block.blockFormat().headingLevel() > 0) {
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        QTextCharFormat heading;
        heading.setForeground(theme.accent);
        cursor.mergeCharFormat(heading);
    }
    styleLinks(block, theme);
}

} // namespace

QString html(const QString &markdown, const Style &style)
{
    QTextDocument document;
    // The HTML names the document's font on its body: the tooltip's, not
    // the application's.
    document.setDefaultFont(QToolTip::font());
    document.setMarkdown(markdown);
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        if (isCodeLine(block))
            block = styleCodeBlock(block, style);
        else
            styleTextBlock(block, style.theme);
    }
    return document.toHtml();
}

} // namespace MarkdownToolTip
