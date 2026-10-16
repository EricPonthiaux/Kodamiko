#include "MarkdownHtml.h"

#include "editor/CodeColorizer.h"
#include "syntax/LanguageRegistry.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm.h>

#include <cstdlib>
#include <vector>

namespace {

constexpr int options = CMARK_OPT_UNSAFE | CMARK_OPT_FOOTNOTES;

QString resource(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

// An Octicon from the resources, with the classes GitHub gives it.
QString octicon(const QString &name, const QString &classes)
{
    QString svg = resource(QStringLiteral(":/markdown/octicons/%1-16.svg").arg(name)).trimmed();
    svg.replace(QLatin1String("<svg "),
                QStringLiteral("<svg class=\"octicon octicon-%1%2\" aria-hidden=\"true\" ")
                    .arg(name, classes.isEmpty() ? QString() : QLatin1Char(' ') + classes));
    return svg;
}

QString render(cmark_node *node, cmark_llist *extensions)
{
    char *html = cmark_render_html(node, options, extensions);
    const QString result = QString::fromUtf8(html);
    std::free(html);
    return result;
}

// Text of a node and its children, without markup.
QString plainText(cmark_node *node)
{
    QString text;
    for (cmark_node *child = cmark_node_first_child(node); child; child = cmark_node_next(child)) {
        const cmark_node_type type = cmark_node_get_type(child);
        if (type == CMARK_NODE_TEXT || type == CMARK_NODE_CODE)
            text += QString::fromUtf8(cmark_node_get_literal(child));
        else if (type == CMARK_NODE_SOFTBREAK || type == CMARK_NODE_LINEBREAK)
            text += QLatin1Char(' ');
        else
            text += plainText(child);
    }
    return text;
}

void replaceWithHtml(cmark_node *node, const QString &html)
{
    cmark_node *block = cmark_node_new(CMARK_NODE_HTML_BLOCK);
    cmark_node_set_literal(block, html.toUtf8().constData());
    cmark_node_replace(node, block);
    cmark_node_free(node);
}

QString highlightedCode(const QString &code, const QString &languageId, CodeColorizer *colorizer)
{
    if (!colorizer)
        return code.toHtmlEscaped();
    QString html;
    int position = 0;
    for (const CodeColorizer::Span &span : colorizer->categorize(code, languageId)) {
        const QString pl = MarkdownHtml::prettyLightsClass(span.category, languageId);
        if (pl.isEmpty())
            continue;
        html += code.mid(position, span.start - position).toHtmlEscaped();
        html += QStringLiteral("<span class=\"%1\">%2</span>").arg(pl, code.mid(span.start, span.length).toHtmlEscaped());
        position = span.start + span.length;
    }
    return html + code.mid(position).toHtmlEscaped();
}

// ```lang blocks: GitHub's "highlight" markup, or a plain block.
QString codeBlockHtml(cmark_node *node, CodeColorizer *colorizer)
{
    const QString info = QString::fromUtf8(cmark_node_get_fence_info(node)).trimmed();
    QString code = QString::fromUtf8(cmark_node_get_literal(node));
    if (code.endsWith(QLatin1Char('\n')))
        code.chop(1);
    const QString name = info.section(QLatin1Char(' '), 0, 0).toLower();
    const QString languageId = CodeColorizer::languageOfFence(info);
    if (name.isEmpty() || languageId == LanguageRegistry::plainText)
        return QStringLiteral("<pre><code>%1</code></pre>\n").arg(code.toHtmlEscaped());
    return QStringLiteral("<div class=\"highlight highlight-source-%1 notranslate position-relative overflow-auto\" "
                          "dir=\"auto\"><pre>%2</pre></div>\n")
        .arg(name.toHtmlEscaped(), highlightedCode(code, languageId, colorizer));
}

// Headings with the anchor GitHub puts in them (shown on hover).
QString headingHtml(cmark_node *node, cmark_llist *extensions, QHash<QString, int> &anchors)
{
    QString anchor = MarkdownHtml::headingAnchor(plainText(node));
    // Repeated titles: "title", "title-1", "title-2"…
    const int seen = anchors.value(anchor, 0);
    anchors.insert(anchor, seen + 1);
    if (seen > 0)
        anchor += QStringLiteral("-%1").arg(seen);
    QString html = render(node, extensions);
    static const QRegularExpression opening(QStringLiteral("^<h([1-6])>"));
    html.replace(opening, QStringLiteral("<h\\1 dir=\"auto\"><a id=\"%1\" class=\"anchor\" aria-hidden=\"true\" tabindex=\"-1\" "
                                         "href=\"#%1\">%2</a>")
                              .arg(anchor.toHtmlEscaped(), octicon(QStringLiteral("link"), {})));
    return html;
}

struct Alert {
    const char *marker;
    const char *kind;
    const char *title;
    const char *icon;
};
const Alert alerts[] = {
    {"[!NOTE]", "note", "Note", "info"},
    {"[!TIP]", "tip", "Tip", "light-bulb"},
    {"[!IMPORTANT]", "important", "Important", "report"},
    {"[!WARNING]", "warning", "Warning", "alert"},
    {"[!CAUTION]", "caution", "Caution", "stop"},
};

// "> [!NOTE]" quotes: GitHub's alert boxes. Empty if `node` is not one.
QString alertHtml(cmark_node *node, cmark_llist *extensions)
{
    cmark_node *paragraph = cmark_node_first_child(node);
    if (!paragraph || cmark_node_get_type(paragraph) != CMARK_NODE_PARAGRAPH)
        return {};
    cmark_node *marker = cmark_node_first_child(paragraph);
    if (!marker || cmark_node_get_type(marker) != CMARK_NODE_TEXT)
        return {};
    // The marker alone on its line.
    cmark_node *after = cmark_node_next(marker);
    if (after && cmark_node_get_type(after) != CMARK_NODE_SOFTBREAK && cmark_node_get_type(after) != CMARK_NODE_LINEBREAK)
        return {};
    const QString text = QString::fromUtf8(cmark_node_get_literal(marker)).trimmed();
    const Alert *alert = nullptr;
    for (const Alert &candidate : alerts) {
        if (text.compare(QLatin1String(candidate.marker), Qt::CaseInsensitive) == 0)
            alert = &candidate;
    }
    if (!alert)
        return {};

    if (after)
        cmark_node_free(after); // unlinks it
    cmark_node_free(marker);
    if (!cmark_node_first_child(paragraph))
        cmark_node_free(paragraph);
    QString content;
    for (cmark_node *child = cmark_node_first_child(node); child; child = cmark_node_next(child))
        content += render(child, extensions);
    return QStringLiteral("<div class=\"markdown-alert markdown-alert-%1\" dir=\"auto\">"
                          "<p class=\"markdown-alert-title\" dir=\"auto\">%2%3</p>%4</div>\n")
        .arg(QLatin1String(alert->kind), octicon(QLatin1String(alert->icon), QStringLiteral("mr-2")),
             QLatin1String(alert->title), content);
}

// cmark-gfm writes task items plainly; GitHub adds classes its style sheet uses.
void addTaskListClasses(QString &html)
{
    static const QRegularExpression item(QStringLiteral("<li><input type=\"checkbox\"( checked=\"\")? disabled=\"\" />"));
    html.replace(item, QStringLiteral("<li class=\"task-list-item\"><input type=\"checkbox\" "
                                      "class=\"task-list-item-checkbox\"\\1 disabled=\"\" />"));
    static const QRegularExpression list(QStringLiteral("<ul>(\\s*<li class=\"task-list-item\">)"));
    html.replace(list, QStringLiteral("<ul class=\"contains-task-list\">\\1"));
}

} // namespace

namespace MarkdownHtml {

QString headingAnchor(const QString &heading)
{
    QString anchor;
    for (const QChar character : heading.trimmed().toLower()) {
        if (character.isLetterOrNumber() || character == QLatin1Char('-') || character == QLatin1Char('_'))
            anchor += character;
        else if (character.isSpace())
            anchor += QLatin1Char('-');
    }
    return anchor;
}

QString prettyLightsClass(const QString &category, const QString &languageId)
{
    // JSON keys are entities on GitHub (green).
    if (languageId == QLatin1String("json") && category.startsWith(QLatin1String("property")))
        return QStringLiteral("pl-ent");
    static const QHash<QString, QString> classes = {
        {"comment", "pl-c"},
        {"keyword", "pl-k"},
        {"type.builtin", "pl-k"},
        {"string", "pl-s"},
        {"string.special", "pl-s"},
        {"escape", "pl-cce"},
        {"number", "pl-c1"},
        {"constant", "pl-c1"},
        {"variable.builtin", "pl-c1"},
        {"function.builtin", "pl-c1"},
        {"property", "pl-c1"},
        {"function", "pl-en"},
        {"type", "pl-en"},
        {"constructor", "pl-en"},
        {"module", "pl-en"},
        {"label", "pl-en"},
        {"tag", "pl-ent"},
        {"attribute", "pl-e"},
        {"heading", "pl-mh"},
        {"diff.plus", "pl-mi1"},
        {"diff.minus", "pl-md"},
    };
    // The most specific known category: "function.method" → "function".
    QString candidate = category;
    while (!candidate.isEmpty()) {
        if (const auto it = classes.constFind(candidate); it != classes.cend())
            return *it;
        const qsizetype dot = candidate.lastIndexOf(QLatin1Char('.'));
        if (dot < 0)
            break;
        candidate.truncate(dot);
    }
    return {};
}

QString body(const QString &markdown, CodeColorizer *colorizer)
{
    cmark_gfm_core_extensions_ensure_registered();
    cmark_parser *parser = cmark_parser_new(options);
    for (const char *name : {"table", "strikethrough", "autolink", "tagfilter", "tasklist"}) {
        if (cmark_syntax_extension *extension = cmark_find_syntax_extension(name))
            cmark_parser_attach_syntax_extension(parser, extension);
    }
    const QByteArray text = markdown.toUtf8();
    cmark_parser_feed(parser, text.constData(), static_cast<size_t>(text.size()));
    cmark_node *document = cmark_parser_finish(parser);
    cmark_llist *extensions = cmark_parser_get_syntax_extensions(parser);

    // Nodes to rewrite, collected first: the tree must not change while walked.
    std::vector<cmark_node *> nodes;
    cmark_iter *iter = cmark_iter_new(document);
    for (cmark_event_type event; (event = cmark_iter_next(iter)) != CMARK_EVENT_DONE;) {
        cmark_node *node = cmark_iter_get_node(iter);
        const cmark_node_type type = cmark_node_get_type(node);
        if (event == CMARK_EVENT_ENTER
            && (type == CMARK_NODE_CODE_BLOCK || type == CMARK_NODE_HEADING || type == CMARK_NODE_BLOCK_QUOTE))
            nodes.push_back(node);
    }
    cmark_iter_free(iter);

    // Innermost first: a code block inside an alert is rewritten before it.
    QHash<QString, int> anchors;
    std::vector<cmark_node *> quotes;
    for (cmark_node *node : nodes) {
        switch (cmark_node_get_type(node)) {
        case CMARK_NODE_CODE_BLOCK:
            replaceWithHtml(node, codeBlockHtml(node, colorizer));
            break;
        case CMARK_NODE_HEADING:
            replaceWithHtml(node, headingHtml(node, extensions, anchors));
            break;
        default:
            quotes.push_back(node);
            break;
        }
    }
    for (auto it = quotes.rbegin(); it != quotes.rend(); ++it) {
        if (const QString html = alertHtml(*it, extensions); !html.isEmpty())
            replaceWithHtml(*it, html);
    }

    QString html = render(document, extensions);
    cmark_node_free(document);
    cmark_parser_free(parser);
    addTaskListClasses(html);
    return html;
}

QString pageColor(bool dark)
{
    return dark ? QStringLiteral("#0d1117") : QStringLiteral("#ffffff");
}

QString page(const QString &body, bool dark)
{
    const QString css = resource(dark ? QStringLiteral(":/markdown/github-markdown-css/github-markdown-dark.css")
                                      : QStringLiteral(":/markdown/github-markdown-css/github-markdown-light.css"));
    // The layout github-markdown-css recommends, on GitHub's page color.
    const QString layout = QStringLiteral(
                               "html, body { margin: 0; background-color: %1; }\n"
                               ".markdown-body { box-sizing: border-box; min-width: 200px; max-width: 980px; "
                               "margin: 0 auto; padding: 45px; }\n"
                               "@media (max-width: 767px) { .markdown-body { padding: 15px; } }\n")
                               .arg(pageColor(dark));
    return QStringLiteral("<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">\n"
                          "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; "
                          "img-src file: data: https: http:; style-src 'unsafe-inline'; font-src file: data:\">\n"
                          "<style>%1</style>\n<style>%2</style>\n</head>\n"
                          "<body><article class=\"markdown-body\">%3</article></body></html>\n")
        .arg(css, layout, body);
}

} // namespace MarkdownHtml
