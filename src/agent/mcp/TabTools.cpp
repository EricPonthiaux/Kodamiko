// SPDX-License-Identifier: GPL-3.0-only

// MCP tools for the editor's tabs, files and pages alike (a review, a
// diagram being drawn…): listing them, showing one, reading a page.

#include "KDKTools.h"
#include "McpRegistry.h"
#include "editor/EditorTabs.h"

#include <QAbstractButton>
#include <QJsonArray>
#include <QLabel>
#include <QTextEdit>

#include <optional>

namespace {

using namespace McpSchema;

// The tab the `index` argument names (1-based, as list_tabs gives it), as
// a QTabWidget index; nullopt when there is no such tab.
std::optional<int> tabIndex(const EditorTabs &tabs, const QJsonObject &arguments)
{
    const int index = arguments.value(QStringLiteral("index")).toInt() - 1;
    if (index < 0 || index >= tabs.count())
        return std::nullopt;
    return index;
}

McpResult noSuchTab(const QJsonObject &arguments)
{
    return McpResult::error(QStringLiteral("There is no tab %1: list_tabs gives the tabs' indexes")
                                .arg(arguments.value(QStringLiteral("index")).toInt()));
}

// What a widget shows as text: a label's (its HTML when it is rich text),
// a text view's as Markdown, a button's in brackets.
QString shownText(const QWidget &widget)
{
    if (const auto *label = qobject_cast<const QLabel *>(&widget))
        return label->text();
    if (const auto *view = qobject_cast<const QTextEdit *>(&widget))
        return view->toMarkdown();
    if (const auto *button = qobject_cast<const QAbstractButton *>(&widget))
        return button->text().isEmpty() ? QString() : QStringLiteral("[%1]").arg(button->text());
    return {};
}

// The texts `page` shows, in the order of its widgets; hidden ones are left
// out, and a text view's own children (its viewport) are not read twice.
QStringList shownTexts(const QWidget &page)
{
    QStringList texts;
    for (const QWidget *widget : page.findChildren<QWidget *>()) {
        if (!widget->isVisibleTo(&page))
            continue;
        const QString text = shownText(*widget).trimmed();
        if (!text.isEmpty())
            texts.append(text);
    }
    return texts;
}

McpResult listTabs(const KDKToolContext &context, const QJsonObject &)
{
    const EditorTabs &tabs = *context.tabs;
    QJsonArray list;
    for (int i = 0; i < tabs.count(); ++i) {
        QJsonObject tab{{QStringLiteral("index"), i + 1},
                        {QStringLiteral("title"), tabs.tabText(i)},
                        {QStringLiteral("current"), i == tabs.currentIndex()}};
        if (const QString path = tabs.filePathOf(tabs.widget(i)); !path.isEmpty())
            tab.insert(QStringLiteral("path"), context.relative(path));
        list.append(tab);
    }
    return McpResult::json(QJsonObject{{QStringLiteral("tabs"), list}});
}

McpResult showTab(const KDKToolContext &context, const QJsonObject &arguments)
{
    const std::optional<int> index = tabIndex(*context.tabs, arguments);
    if (!index)
        return noSuchTab(arguments);
    // The tabs are in the Edit mode only.
    if (context.showEditor)
        context.showEditor();
    context.tabs->setCurrentIndex(*index);
    return McpResult::message(QStringLiteral("Showing the tab %1").arg(context.tabs->tabText(*index)));
}

McpResult getTabText(const KDKToolContext &context, const QJsonObject &arguments)
{
    const std::optional<int> index = tabIndex(*context.tabs, arguments);
    if (!index)
        return noSuchTab(arguments);
    QWidget *page = context.tabs->widget(*index);
    if (const QString path = context.tabs->filePathOf(page); !path.isEmpty())
        return McpResult::error(
            QStringLiteral("The tab %1 shows a file: get_file_text reads it").arg(context.relative(path)));
    return McpResult::json(QJsonObject{{QStringLiteral("title"), context.tabs->tabText(*index)},
                                       {QStringLiteral("texts"), QJsonArray::fromStringList(shownTexts(*page))}});
}

} // namespace

namespace KDKTools {

void registerTabTools(McpRegistry &registry, const KDKToolContext &context)
{
    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("list_tabs"),
         .title = QStringLiteral("Editor tabs"),
         .description = QStringLiteral("Every tab of Kodamiko's editor, in order: the files' (with their path) and "
                                       "the pages that show no file, such as a review or a diagram being drawn. "
                                       "Its index is what show_tab and get_tab_text take."),
         .inputSchema = object({})},
        handledBy(context, listTabs)));

    registry.addTool(McpTool::acting(
        {.name = QStringLiteral("show_tab"),
         .title = QStringLiteral("Show a tab"),
         .description = QStringLiteral("Makes a tab of the editor the current one, in the Edit mode, for the user to "
                                       "see it."),
         .inputSchema = object({{QStringLiteral("index"), integer(QStringLiteral("Tab index, 1-based (see "
                                                                                 "list_tabs)"))}},
                               {QStringLiteral("index")})},
        handledBy(context, showTab)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("get_tab_text"),
         .title = QStringLiteral("Tab text"),
         .description = QStringLiteral("The texts a page tab shows (a review's findings, a message…), in the order "
                                       "of its widgets: labels, text views as Markdown, [buttons]. For a file's tab, "
                                       "get_file_text reads the file."),
         .inputSchema = object({{QStringLiteral("index"), integer(QStringLiteral("Tab index, 1-based (see "
                                                                                 "list_tabs)"))}},
                               {QStringLiteral("index")})},
        handledBy(context, getTabText)));
}

} // namespace KDKTools
