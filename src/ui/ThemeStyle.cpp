// SPDX-License-Identifier: GPL-3.0-only

#include "ThemeStyle.h"

#include "PopupMetrics.h"
#include "UiMetrics.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QWidget>

#include <utility>

namespace {

QString writeIcon(const QString &name, const QString &svg)
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/theme");
    if (!QDir().mkpath(folder))
        return {};
    QString path = folder + QLatin1Char('/') + name;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly) && file.readAll() == svg.toUtf8())
        return path;
    file.close();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(svg.toUtf8()) < 0)
        return {};
    return path;
}

QString crossIcon(const QString &name, const QColor &color)
{
    return writeIcon(
        name, QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>"
                             "<path d='M4.5 4.5l7 7M11.5 4.5l-7 7' stroke='%1' stroke-width='1.6' "
                             "stroke-linecap='round'/></svg>")
                  .arg(color.name()));
}

// The mark of a checked check box, on the accent.
QString checkIcon(const QString &name, const QColor &color)
{
    return writeIcon(
        name, QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14' viewBox='0 0 14 14'>"
                             "<path d='M3.2 7.3l2.5 2.5 5.1-5.6' fill='none' stroke='%1' stroke-width='1.8' "
                             "stroke-linecap='round' stroke-linejoin='round'/></svg>")
                  .arg(color.name()));
}

// The tab bar's scroll arrows: the style's own are a few pixels wide.
QString chevronIcon(const QString &name, const QColor &color, QStringView path)
{
    return writeIcon(
        name, QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>"
                             "<path d='%1' fill='none' stroke='%2' stroke-width='1.8' stroke-linecap='round' "
                             "stroke-linejoin='round'/></svg>")
                  .arg(path, color.name()));
}

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(float(alpha));
    return color;
}

void inheritApplicationFont()
{
    const QList<QWidget *> widgets = QApplication::allWidgets();
    for (QWidget *widget : widgets) {
        if (!widget->testAttribute(Qt::WA_SetFont))
            widget->setFont(QFont());
    }
}

void setStyleSheetKeepingFonts(const QString &sheet)
{
    QList<std::pair<QWidget *, QFont>> ownFonts;
    const QList<QWidget *> widgets = QApplication::allWidgets();
    for (QWidget *widget : widgets) {
        if (widget->testAttribute(Qt::WA_SetFont))
            ownFonts.append({widget, widget->font()});
    }
    qApp->setStyleSheet(sheet);
    QApplication::setFont(QApplication::font());
    inheritApplicationFont();
    for (const auto &[widget, font] : std::as_const(ownFonts))
        widget->setFont(font);
}

// The theme apply() set last.
Theme &appliedTheme()
{
    static Theme theme = Theme::kodamiko();
    return theme;
}

} // namespace

namespace ThemeStyle {

QColor mixed(const QColor &base, const QColor &tint, qreal share)
{
    return QColor::fromRgbF(float(base.redF() + (tint.redF() - base.redF()) * share),
                            float(base.greenF() + (tint.greenF() - base.greenF()) * share),
                            float(base.blueF() + (tint.blueF() - base.blueF()) * share));
}

QPalette palette(const Theme &theme)
{
    QPalette palette;
    palette.setColor(QPalette::Window, theme.window);
    palette.setColor(QPalette::WindowText, theme.foreground);
    palette.setColor(QPalette::Base, theme.background);
    palette.setColor(QPalette::AlternateBase, theme.alternateBase);
    palette.setColor(QPalette::Text, theme.foreground);
    palette.setColor(QPalette::Button, theme.button);
    palette.setColor(QPalette::ButtonText, theme.foreground);
    palette.setColor(QPalette::Highlight, theme.highlight);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, theme.alternateBase);
    palette.setColor(QPalette::ToolTipText, theme.foreground);
    palette.setColor(QPalette::PlaceholderText, theme.marginForeground);
    palette.setColor(QPalette::Link, theme.accent);
    palette.setColor(QPalette::Accent, theme.accent);
    palette.setColor(QPalette::Light, theme.isDark ? theme.button.lighter(130) : Qt::white);
    palette.setColor(QPalette::Midlight, theme.isDark ? theme.button.lighter(115) : theme.alternateBase);
    palette.setColor(QPalette::Mid, theme.border);
    palette.setColor(QPalette::Dark, theme.isDark ? theme.window.darker(130) : theme.border.darker(115));
    palette.setColor(QPalette::Shadow, Qt::black);
    palette.setColor(QPalette::Disabled, QPalette::Text, theme.marginForeground);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, theme.marginForeground);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, theme.marginForeground);
    return palette;
}

QString styleSheet(const Theme &theme)
{
    QString sheet = QStringLiteral(R"(
QToolTip { color: @text; background: @panel; border: 1px solid @border; border-radius: @frameRadiuspx; padding: 4px 6px; }
QMenuBar { background: @window; border-bottom: 1px solid @line; }
QMenuBar::item { padding: 5px 10px; border-radius: 5px; background: transparent; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: @hover; color: @text; }
QMenu { background: @panel; color: @text; border: 1px solid @border; border-radius: @frameRadiuspx; padding: 6px; }
QMenu::item { padding: 6px 28px 6px 22px; border: 1px solid transparent; border-radius: @menuRowRadiuspx; }
QMenu::icon { padding-left: 32px; }
QMenu#projectMenu::icon { padding-left: 44px; }
QMenu::item:selected { color: @text; background: @slate; }
QMenu::item:disabled { color: @muted; }
QMenu::separator { height: 1px; background: @separator; margin: 6px 10px; }
QMenu#diagramPresets { icon-size: 32px; }
QLineEdit, QPlainTextEdit, QTextBrowser { background: @base; border: 1px solid @border; border-radius: 3px; padding: 4px 8px; selection-background-color: @highlight; }
QLineEdit:focus, QPlainTextEdit:focus { border: 1px solid @accent; }
QPushButton { background: @button; border: 1px solid @border; border-radius: 3px; padding: 4px 14px; }
QPushButton:hover { border-color: @highlight; }
QPushButton:pressed { background: @highlight; }
QPushButton:default { color: @onAccent; border: 1px solid @accent; background: @accent; }
QPushButton#reviewPrimary { color: @reviewOnAccent; border: 1px solid @reviewAccent; background: @reviewAccent; }
QPushButton#reviewPrimary:disabled { color: @muted; border-color: @line; background: @button; }
QLabel#reviewKind { color: @reviewAccent; }
QLabel#reviewMuted { color: @muted; }
QLabel#reviewSuccessSymbol { color: @reviewSuccess; background: @reviewSuccessTint; border: 1px solid @line; border-radius: 28px; }
QLabel#reviewFailureSymbol { color: @reviewError; background: @reviewErrorTint; border: 1px solid @line; border-radius: 9px; }
QLabel#reviewError, QLabel#reviewWarning { padding: 4px 7px; border-radius: 4px; }
QLabel#reviewError { color: @reviewError; background: @reviewErrorTint; }
QLabel#reviewWarning { color: @reviewWarning; background: @reviewWarningTint; }
QToolButton#reviewLine { color: @muted; background: transparent; padding: 0; }
QToolButton#reviewLine:hover { color: @reviewAccent; }
QPushButton:disabled { color: @muted; border-color: @line; }
QToolButton { border-radius: 3px; padding: 2px 5px; }
QToolButton:hover { background: @button; }
QToolButton:checked { background: @selection; color: @accentText; }
QFrame#previewToolbar { background: @base; border: none; border-bottom: 1px solid @line; }
QLabel#previewFormat { color: @muted; background: transparent; }
QFrame#modeSelector, QFrame#hunkActions { background: @window; border: 1px solid @line; border-radius: 6px; }
QFrame#modeSelector QToolButton, QFrame#hunkActions QToolButton { color: @muted; background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 3px 8px; min-height: 16px; }
QFrame#modeSelector QToolButton:hover, QFrame#hunkActions QToolButton:hover { color: @text; background: @hover; }
QFrame#modeSelector QToolButton:checked { color: @accentText; background: @modeActive; border-color: @modeBorder; }
QFrame#modeSelector QToolButton:pressed, QFrame#hunkActions QToolButton:pressed { background: @selection; }
QFrame#modeSelector QToolButton:focus, QFrame#hunkActions QToolButton:focus { border-color: @accent; }
QTabWidget::pane { border: none; }
QTabBar::tab { background: transparent; color: @muted; padding: 8px 16px; border: none; }
QTabBar::tab:hover { color: @text; background: @panel; }
QTabBar::tab:selected { color: @accentText; background: @base; border-bottom: 2px solid @accent; }
QTabBar::tab:bottom:selected { border-bottom: none; border-top: 2px solid @accent; }
QTabBar#goToTabs::tab { background: @tabs; color: @muted; padding: 6px 12px; min-width: 56px; border: none; }
QTabBar#goToTabs::tab:first { border-top-left-radius: 3px; }
QTabBar#goToTabs::tab:last { border-top-right-radius: 3px; }
QTabBar#goToTabs::tab:hover { background: @hover; color: @text; border-top-left-radius: 3px; border-top-right-radius: 3px; }
QTabBar#goToTabs::tab:selected { background: @base; color: @accentText; border: 1px solid @line; border-bottom: none; border-top: 2px solid @accent; border-top-left-radius: 3px; border-top-right-radius: 3px; }
EditorTabs QTabBar, QTabWidget#terminalTabs QTabBar, SidePanel QFrame#viewHeader { background: @window; }
EditorTabs QTabBar::tab, QTabWidget#terminalTabs QTabBar::tab, SidePanel QFrame#viewHeaderTab { margin-top: @tabTopMarginpx; border: 1px solid transparent; border-bottom: none; border-top-left-radius: 4px; border-top-right-radius: 4px; }
EditorTabs QTabBar::tab, QTabWidget#terminalTabs QTabBar::tab { background: transparent; color: @muted; padding: 8px 12px 9px; min-width: 64px; max-width: 240px; }
QTabWidget#terminalTabs QTabBar::tab:first, QTabWidget#terminalTabs QTabBar::tab:only-one { margin-left: 6px; }
EditorTabs QTabBar::tab:hover, QTabWidget#terminalTabs QTabBar::tab:hover { background: @hover; color: @text; }
EditorTabs QTabBar::tab:selected, QTabWidget#terminalTabs QTabBar::tab:selected, SidePanel QFrame#viewHeaderTab { background: @base; color: @text; border-color: @line; border-bottom: none; }
SidePanel > QWidget { background: @base; }
SidePanel QFrame#viewHeader { border: none; }
SidePanel QFrame#viewHeaderTab { border-left: none; border-top-left-radius: 0; }
SidePanel[edge="right"] QFrame#viewHeaderTab { border-left: 1px solid @line; border-right: none; border-top-left-radius: 4px; border-top-right-radius: 0; }
EditorTabs QTabBar[preview="true"]::tab:last { font-style: italic; }
QTabBar#modeTabs::tab { background: transparent; color: @muted; padding: 0 10px; margin: 3px 2px; min-width: 0; border: 1px solid transparent; border-radius: 4px; }
QTabBar#modeTabs::tab:first { margin-left: 3px; }
QTabBar#modeTabs::tab:last { margin-right: 3px; }
QTabBar#modeTabs::tab:hover { background: @hover; color: @text; }
QTabBar#modeTabs::tab:selected { background: @modeActive; color: @accentText; border: 1px solid @modeBorder; }
QTabBar#modeTabs::tab:selected:hover { background: @modeHover; }
QTabBar::scroller { width: 56px; }
QTabBar QToolButton { background: @window; border: none; border-radius: 0; padding: 0; }
QTabBar QToolButton:hover { background: @button; }
QTabBar QToolButton:disabled { background: @window; }
QWidget#sideStrip { background: @window; border: none; }
QFrame#foldedPane { background: @window; border: none; border-right: 1px solid @line; }
QWidget#diagramToolbar { background: @base; border-bottom: 1px solid @line; }
QWidget#diagramToolbar QToolButton { color: @text; background: transparent; border: 1px solid transparent; border-radius: 5px; padding: 3px 8px; }
QWidget#diagramToolbar QToolButton:hover { background: @hover; }
QWidget#diagramToolbar QToolButton:pressed { background: @selection; }
QWidget#diagramToolbar QToolButton:focus { border-color: @accent; }
QWidget#diagramToolbar QToolButton:disabled { color: @muted; }
QWidget#diagramToolbar QToolButton:checked { background: @modeActive; border-color: @modeBorder; }
QWidget#diagramToolbar QToolButton:checked:hover { background: @hover; }
QFrame#diagramToolbarDivider { background: @line; }
QFrame#diagramZoom { background: @window; border: 1px solid @line; border-radius: 6px; }
QWidget#diagramToolbar QFrame#diagramZoom QToolButton { border: 1px solid transparent; border-radius: 4px; padding: 1px 5px; }
QWidget#diagramToolbar QFrame#diagramZoom QToolButton:focus { border-color: @accent; }
QWidget#diagramInspectorHeader { background: @base; border-bottom: 1px solid @line; }
QTabBar#sideTabs { background: transparent; }
QTabBar#sideTabs::tab { padding: 14px 6px; margin: 2px 0 2px 4px; background: transparent; border: 1px solid transparent; border-right: none; border-top-left-radius: 4px; border-bottom-left-radius: 4px; color: @muted; }
QTabBar#sideTabs::tab:hover { background: @hover; color: @text; }
QTabBar#sideTabs::tab:selected { background: @base; border-color: @line; border-top-right-radius: 0; border-bottom-right-radius: 0; color: @text; }
QTabBar#sideTabs::tab:first { margin-top: 0; }
QTabBar#sideTabs::tab:right { margin: 2px 4px 2px 0; border: 1px solid transparent; border-left: none; border-radius: 0; border-top-right-radius: 4px; border-bottom-right-radius: 4px; }
QTabBar#sideTabs::tab:right:hover { background: @hover; color: @text; }
QTabBar#sideTabs::tab:right:selected { background: @base; border-color: @line; border-top-left-radius: 0; border-bottom-left-radius: 0; color: @text; }
QTabBar#sideTabs::tab:right:first { margin-top: 0; }
QHeaderView::section { background: @panel; color: @muted; border: none; border-bottom: 1px solid @line; padding: 4px 6px; }
QTreeView, QTreeWidget, QListView { background: @base; border: none; outline: 0; }
QTreeView { padding-top: 6px; }
QTreeView::item:selected, QTreeWidget::item:selected { background: @selection; color: @text; }
QTreeView::item:selected:active, QTreeWidget::item:selected:active { color: @text; background: @slate; }
QTreeView::item:hover { background: @hover; }
QListWidget#projectRecipes::item { padding: 10px 14px; margin: 3px 0; border-radius: 2px; }
QListWidget#projectRecipes::item:hover { background: @hover; }
QListWidget#projectRecipes::item:selected { background: @slate; color: @text; }
QFrame#card { background: @panel; border: 1px solid @line; border-radius: 4px; }
QFrame#reviewReadingCard { background: @panel; border: 1px solid @line; border-radius: 7px; }
QFrame#viewTitleBar { border: none; border-bottom: 1px solid @line; }
QFrame#fileActions { border: none; border-bottom: 1px solid @line; }
QFrame#commitPanel { border: none; border-top: 1px solid @line; }
QLabel#historyMuted { color: @muted; }
QToolButton#linkButton { padding: 0; color: @accent; }
QToolButton#linkButton:hover { background: transparent; text-decoration: underline; }
QFrame#diffHeader { border: none; border-bottom: 1px solid @line; background: @window; }
QFrame#filePager { margin: 0 8px; background: @base; border: 1px solid @line; border-radius: 6px; }
QFrame#filePager QToolButton { background: @button; border: 1px solid @border; border-radius: 4px; padding: 0 3px; }
QFrame#filePager QToolButton:hover { background: @hover; border-color: @accent; }
QFrame#filePager QToolButton:pressed { background: @selection; }
QFrame#filePager QToolButton:disabled { background: transparent; border-color: transparent; }
QFrame#filePager QLabel { color: @muted; background: transparent; }
QFrame#card QPlainTextEdit { background: @base; }
QStatusBar { background: transparent; border-top: 1px solid @line; color: @muted; }
TitleBar QMenuBar { background: transparent; border: none; }
TitleBar QMenuBar::item { padding: 6px 10px; border-radius: 5px; }
TitleBar QMenuBar::item:selected, TitleBar QMenuBar::item:pressed { background: @hover; color: @text; }
TitleBar QMenuBar QToolButton { padding: 0; border: none; }
TitleBar QToolButton#titleActionsOverflow { padding: 0; border: none; border-radius: 4px; background: transparent; }
TitleBar QToolButton#titleActionsOverflow:hover, TitleBar QToolButton#titleActionsOverflow:pressed { background: @hover; }
TitleBar QToolButton#titleActionsOverflow::menu-indicator { image: none; width: 0; }
TitleBar QFrame#titleProjectGroup { background: @panel; border: 1px solid @border; border-radius: 4px; }
TitleBar QFrame#titleActions { background: transparent; border: none; }
TitleBar QToolButton#projectChip { padding: 0 8px; border: none; border-radius: 3px; background: @projectTint; }
TitleBar QToolButton#projectChip:hover, TitleBar QToolButton#projectChip:pressed { background: @projectHover; }
TitleBar QToolButton#projectChip::menu-indicator { image: none; width: 0; }
TitleBar QToolButton#titleAction { padding: 0; border: none; background: transparent; border-radius: 3px; }
TitleBar QToolButton#titleAction:hover, TitleBar QToolButton#titleAction:pressed { background: @hover; }
QFrame#runBlock { background: transparent; border: none; }
QFrame#runBlock QToolButton { background: transparent; border: none; border-radius: 3px; padding: 0 8px; }
QFrame#runBlock QToolButton#runAction { padding: 0; }
QFrame#runBlock QToolButton:hover, QFrame#runBlock QToolButton:pressed { background: @hover; }
QFrame#runBlock QToolButton::menu-indicator { image: none; width: 0; }
QFrame#runDivider, QFrame#projectDivider { background: @border; border: none; }
TitleBar QFrame#titleDivider { background: @line; }
QFrame#statusDivider { background: @line; border: none; }
QToolButton#chip { padding: 4px 9px; border-radius: 3px; }
QToolButton#chip:hover { background: @button; }
QToolButton#chip::menu-indicator { image: none; width: 0; }
QStatusBar QToolButton#chip { padding: 1px 9px; background: @hover; border: 1px solid @line; }
QStatusBar QToolButton#chip:hover { background: @button; }
QFrame#statusPopup, QFrame#changePopup { background: @panel; border: 1px solid @border; border-radius: @frameRadiuspx; }
QLabel#changeSummary { color: @muted; }
QLineEdit#popupSearch, QLineEdit#popupSearch:focus { background: transparent; border: none; border-bottom: 1px solid @line; border-radius: 0; padding: 7px 8px; }
QFrame#popupSearchBar { border: none; border-bottom: 1px solid @line; }
QFrame#popupSearchBar QLineEdit#popupSearch { border-bottom: none; }
QLabel#findSummary { color: @muted; }
QTreeWidget#popupList { background: transparent; padding: 4px 6px; }
QTreeWidget#popupList::item { padding: 3px 4px; border-radius: @rowRadiuspx; }
QTreeWidget#popupList::item:selected { background: @slate; color: @text; }
QWidget#statusGroup QToolButton { padding: 2px 7px; border: 1px solid transparent; border-radius: 4px; }
QWidget#statusGroup QToolButton:hover, QToolButton#statusOverflow:hover { background: @hover; border-color: @line; }
QWidget#statusGroup QToolButton#chip { background: @hover; border-color: @line; }
QLabel#statusHeading { color: @muted; font-weight: 600; }
QToolButton#statusOverflow { padding: 0; border: 1px solid transparent; border-radius: 4px; }
QFrame#viewHeader { border: none; border-bottom: 1px solid @line; }
QFrame#viewHeaderTab { border: none; }
QToolButton#viewSelector { padding: 4px 6px; border-radius: 3px; }
QToolButton#viewSelector:hover, QToolButton#viewSelector:pressed { background: @button; }
QToolButton#viewSelector::menu-indicator { image: none; width: 0; }
QStatusBar::item { border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:vertical { background: @border; border-radius: 3px; min-height: 28px; margin: 2px; }
QScrollBar::handle:horizontal { background: @border; border-radius: 3px; min-width: 28px; margin: 2px; }
QScrollBar::handle:hover { background: @highlight; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QCheckBox::indicator:checked { background: @accent; border: 1px solid @accent; border-radius: 2px; }
QCheckBox::indicator:unchecked { background: @base; border: 1px solid @border; border-radius: 2px; }
QComboBox { background: @button; border: 1px solid @border; border-radius: 3px; padding: 4px 8px; }
QComboBox:focus, QComboBox:on { border-color: @accent; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView { background: @panel; border: 1px solid @border; selection-background-color: @slate; selection-color: @text; outline: 0; }
)");
    // The popups' shared sizes (PopupMetrics), then the colors.
    sheet.replace(QStringLiteral("@frameRadius"), QString::number(PopupMetrics::frameRadius));
    sheet.replace(QStringLiteral("@rowRadius"), QString::number(PopupMetrics::rowRadius));
    sheet.replace(QStringLiteral("@menuRowRadius"), QString::number(PopupMetrics::menuRowRadius));
    sheet.replace(QStringLiteral("@tabTopMargin"), QString::number(UiMetrics::tabTopMargin));
    const QList<std::pair<QString, QColor>> colors = {
        {"@reviewOnAccent", theme.isDark ? theme.background : theme.onAccent},
        {"@reviewAccent", theme.isDark ? mixed(theme.accent, theme.foreground, 0.5) : theme.accent},
        {"@reviewSuccessTint", withAlpha(theme.isDark ? QColor("#b5dba1") : QColor("#397647"), 0.07)},
        {"@reviewSuccess", theme.isDark ? QColor("#b5dba1") : QColor("#397647")},
        {"@reviewErrorTint", withAlpha(theme.error, 0.08)},
        {"@reviewError", theme.error},
        {"@reviewWarningTint", withAlpha(theme.warning, 0.08)},
        {"@reviewWarning", theme.warning},
        {"@accentText", theme.accentText},
        {"@accent", theme.accent},
        {"@onAccent", theme.onAccent},
        {"@text", theme.foreground},
        {"@muted", theme.mutedText},
        {"@window", theme.window},
        {"@panel", theme.alternateBase},
        {"@base", theme.background},
        {"@button", theme.button},
        {"@border", theme.border},
        {"@line", theme.line},
        {"@hover", theme.hover},
        {"@selection", theme.selection},
        {"@highlight", theme.highlight},
        // The inactive editor, terminal and mode tabs, set back from the window as in KDE's Breeze.
        {"@tabs", theme.window.darker(120)},
        {"@slate", selectionColor(theme)},
        {"@separator", withAlpha(theme.line, 0.7)},
        // The checked segment of a mode selector (preview, diagram panel).
        {"@modeActive", mixed(theme.window, theme.accent, 0.16)},
        {"@modeBorder", mixed(theme.window, theme.accent, 0.35)},
        {"@modeHover", mixed(theme.window, theme.accent, 0.23)},
        // The project is the tinted segment of the shared project/runner frame.
        {"@projectTint", mixed(theme.alternateBase, theme.accent, 0.08)},
        {"@projectHover", mixed(theme.alternateBase, theme.accent, 0.18)},
    };
    for (const auto &[name, color] : colors)
        sheet.replace(name, color.name(color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb));

    const QString cross = crossIcon(QStringLiteral("close-%1.svg").arg(theme.name), theme.mutedText);
    const QString crossHover = crossIcon(QStringLiteral("close-%1-hover.svg").arg(theme.name), theme.foreground);
    if (!cross.isEmpty() && !crossHover.isEmpty())
        // The margin is taken from the button's own size: AppStyle widens the
        // button by the right margin, and any other margin would shrink the cross.
        sheet += QStringLiteral("QTabBar::close-button { image: url(%1); subcontrol-position: right; border-radius: "
                                "3px; margin: 0 8px 0 0; }\n"
                                "QTabBar::close-button:hover { image: url(%2); background: %3; }\n")
                     .arg(cross, crossHover, theme.hover.name());

    if (const QString check = checkIcon(QStringLiteral("check-%1.svg").arg(theme.name), theme.onAccent);
        !check.isEmpty())
        sheet += QStringLiteral("QCheckBox::indicator { width: 14px; height: 14px; }\n"
                                "QCheckBox::indicator:checked { image: url(%1); }\n")
                     .arg(check);

    const QString left =
        chevronIcon(QStringLiteral("left-%1.svg").arg(theme.name), theme.foreground, u"M10 3.5 5.5 8l4.5 4.5");
    const QString right =
        chevronIcon(QStringLiteral("right-%1.svg").arg(theme.name), theme.foreground, u"M6 3.5 10.5 8 6 12.5");
    if (!left.isEmpty() && !right.isEmpty())
        sheet += QStringLiteral("QTabBar QToolButton::left-arrow { image: url(%1); width: 16px; height: 16px; }\n"
                                "QTabBar QToolButton::right-arrow { image: url(%2); width: 16px; height: 16px; }\n")
                     .arg(left, right);
    // The menu overflow button is only 12 px wide: the general tool-button
    // padding hides its icon, and the platform's icon can be dark on dark.
    const QString overflow = chevronIcon(QStringLiteral("menu-overflow-%1.svg").arg(theme.name), theme.foreground,
                                         u"M3 4l4 4-4 4M8 4l4 4-4 4");
    if (!overflow.isEmpty())
        sheet += QStringLiteral("TitleBar QMenuBar QToolButton { image: url(%1); }\n").arg(overflow);
    // Styling the drop-down hides Qt's arrow: a chevron in its place.
    const QString down = chevronIcon(QStringLiteral("down-%1.svg").arg(theme.name), theme.mutedText, u"M4 6l4 4 4-4");
    if (!down.isEmpty())
        sheet +=
            QStringLiteral("QComboBox::down-arrow { image: url(%1); width: 12px; height: 12px; }\n"
                           "QWidget#diagramToolbar QToolButton#diagramExport { padding-right: 18px; }\n"
                           "QToolButton#diagramExport::menu-indicator { image: url(%1); width: 10px; height: 10px; "
                           "subcontrol-position: right center; subcontrol-origin: padding; right: 4px; }\n")
                .arg(down);
    return sheet;
}

QColor selectionColor(const Theme &theme)
{
    return mixed(theme.alternateBase, theme.accent, 0.3);
}

void apply(const Theme &theme)
{
    appliedTheme() = theme;
    // Setting either one restyles every widget of the application, even
    // when nothing changes (a window opening on the current theme).
    const QPalette colors = palette(theme);
    if (QApplication::palette() != colors)
        QApplication::setPalette(colors);
    const QString sheet = styleSheet(theme);
    if (qApp->styleSheet() != sheet)
        setStyleSheetKeepingFonts(sheet);
}

const Theme &applied()
{
    return appliedTheme();
}

void setStyleSheetIfChanged(QWidget *widget, const QString &sheet)
{
    if (widget->styleSheet() != sheet)
        widget->setStyleSheet(sheet);
}

void applyFont(const QFont &font)
{
    QApplication::setFont(font);
    inheritApplicationFont();
}

} // namespace ThemeStyle
