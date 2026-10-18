#include "ThemeStyle.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace {

// A style sheet can only take images from files: the tab close cross is
// written to the cache folder. Returns its path, empty if it cannot be written.
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

// `base` with `share` of `tint` mixed in, opaque.
QColor mixed(const QColor &base, const QColor &tint, qreal share)
{
    return QColor::fromRgbF(float(base.redF() + (tint.redF() - base.redF()) * share),
                            float(base.greenF() + (tint.greenF() - base.greenF()) * share),
                            float(base.blueF() + (tint.blueF() - base.blueF()) * share));
}

} // namespace

namespace ThemeStyle {

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
    palette.setColor(QPalette::ToolTipBase, theme.button);
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

// Every theme's controls: rounded fields and menus, focus outlines in the
// accent color, a gradient on the selected menu items and rows, an underlined
// current tab, thin scroll bars; the colors come from the theme.
QString styleSheet(const Theme &theme)
{
    QString sheet = QStringLiteral(R"(
QToolTip { color: @text; background: @button; border: 1px solid @border; border-radius: 3px; padding: 4px 6px; }
QMenuBar { background: @window; border-bottom: 1px solid @line; }
QMenuBar::item { padding: 4px 10px; border-radius: 3px; background: transparent; }
QMenuBar::item:selected { background: @button; color: @accentText; }
QMenu { background: @panel; border: 1px solid @border; border-radius: 4px; padding: 4px; }
QMenu::item { padding: 5px 28px 5px 22px; border-radius: 2px; }
QMenu::icon { padding-left: 32px; }
QMenu::item:selected { color: @text; background: @slate; }
QMenu::item:disabled { color: @muted; }
QMenu::separator { height: 1px; background: @separator; margin: 5px 12px; }
QLineEdit, QPlainTextEdit, QTextBrowser { background: @base; border: 1px solid @border; border-radius: 3px; padding: 4px 8px; selection-background-color: @highlight; }
QLineEdit:focus, QPlainTextEdit:focus { border: 1px solid @accent; }
QPushButton { background: @button; border: 1px solid @border; border-radius: 3px; padding: 4px 14px; }
QPushButton:hover { border-color: @highlight; }
QPushButton:pressed { background: @highlight; }
QPushButton:default { color: white; border: 1px solid @accent; background: @accent; }
QPushButton:disabled { color: @muted; border-color: @line; }
QToolButton { border-radius: 3px; padding: 2px 5px; }
QToolButton:hover { background: @button; }
QToolButton:checked { background: @selection; color: @accentText; }
QTabWidget::pane { border: none; }
QTabBar::tab { background: transparent; color: @muted; padding: 8px 16px; border: none; }
QTabBar::tab:hover { color: @text; background: @panel; }
QTabBar::tab:selected { color: @accentText; background: @base; border-bottom: 2px solid @accent; }
QTabBar::tab:bottom:selected { border-bottom: none; border-top: 2px solid @accent; }
EditorTabs QTabBar::tab, QTabWidget#terminalTabs QTabBar::tab { background: @tabs; color: @muted; padding: 6px 12px; min-width: 56px; border: none; }
EditorTabs QTabBar::tab:first, QTabWidget#terminalTabs QTabBar::tab:first { border-top-left-radius: 3px; }
EditorTabs QTabBar::tab:last, QTabWidget#terminalTabs QTabBar::tab:last { border-top-right-radius: 3px; }
EditorTabs QTabBar::tab:hover, QTabWidget#terminalTabs QTabBar::tab:hover { background: @hover; color: @text; }
EditorTabs QTabBar::tab:selected, QTabWidget#terminalTabs QTabBar::tab:selected { background: @base; color: @accentText; border: 1px solid @line; border-bottom: none; border-top: 2px solid @accent; border-top-left-radius: 3px; border-top-right-radius: 3px; }
EditorTabs QTabBar[preview="true"]::tab:last { font-style: italic; }
QTabBar#modeTabs::tab { background: @tabs; color: @muted; padding: 6px 12px; min-width: 56px; border: none; }
QTabBar#modeTabs::tab:first { border-bottom-left-radius: 3px; }
QTabBar#modeTabs::tab:last { border-bottom-right-radius: 3px; }
QTabBar#modeTabs::tab:hover { background: @hover; color: @text; }
QTabBar#modeTabs::tab:selected { background: @base; color: @accentText; border: 1px solid @line; border-top: none; border-bottom: 2px solid @accent; border-bottom-left-radius: 3px; border-bottom-right-radius: 3px; }
QTabBar::scroller { width: 56px; }
QTabBar QToolButton { background: @window; border: none; border-radius: 0; padding: 0; }
QTabBar QToolButton:hover { background: @button; }
QTabBar QToolButton:disabled { background: @window; }
QWidget#sideStrip { background: @window; border-right: 1px solid @border; }
QWidget#viewToolbar { background: @window; border-bottom: 1px solid @border; }
QTabBar#sideTabs { background: transparent; }
QTabBar#sideTabs::tab { padding: 14px 6px; margin: 2px 4px; background: transparent; border: 1px solid transparent; border-radius: 3px; color: @muted; }
QTabBar#sideTabs::tab:hover { background: transparent; border-color: @accent; color: @text; }
QTabBar#sideTabs::tab:selected { background: @checked; border-color: @border; color: @accentText; }
QTabBar#sideTabs::tab:selected:hover { border-color: @accent; }
QHeaderView::section { background: @panel; color: @muted; border: none; border-bottom: 1px solid @line; padding: 4px 6px; }
QTreeView, QTreeWidget, QListView { background: @base; border: none; outline: 0; }
QTreeView::item:selected, QTreeWidget::item:selected { background: @selection; color: @text; }
QTreeView::item:selected:active, QTreeWidget::item:selected:active { color: @text; background: @slate; }
QTreeView::item:hover { background: @hover; }
QFrame#card { background: @panel; border: 1px solid @line; border-radius: 4px; }
QFrame#viewTitleBar { border: none; border-bottom: 1px solid @line; }
QFrame#fileActions { border: none; border-bottom: 1px solid @line; }
QFrame#commitPanel { border: none; border-top: 1px solid @line; }
QLabel#historyMuted { color: @muted; }
QToolButton#linkButton { padding: 0; color: @accent; }
QToolButton#linkButton:hover { background: transparent; text-decoration: underline; }
QFrame#diffHeader { border: none; border-bottom: 1px solid @line; background: @window; }
QToolButton#hunkAction { background: @button; border: 1px solid @border; border-radius: 3px; padding: 0 8px; }
QToolButton#hunkAction:hover { border-color: @highlight; }
QFrame#card QPlainTextEdit { background: @base; }
QStatusBar { background: transparent; border-top: 1px solid @line; color: @muted; }
TitleBar QMenuBar { background: transparent; border: none; }
TitleBar QMenuBar::item { padding: 6px 10px; border-radius: 3px; }
TitleBar QMenuBar::item:selected, TitleBar QMenuBar::item:pressed { background: @hover; color: @text; }
TitleBar QToolButton#chip:hover, TitleBar QToolButton#chip:pressed { background: @hover; }
TitleBar QToolButton#projectChip { padding: 0 8px; border: none; border-radius: 3px; background: transparent; }
TitleBar QToolButton#projectChip:hover, TitleBar QToolButton#projectChip:pressed { background: @hover; }
TitleBar QToolButton#projectChip::menu-indicator { image: none; width: 0; }
TitleBar QToolButton#titleAction { padding: 0; border-radius: 3px; }
TitleBar QToolButton#titleAction:hover, TitleBar QToolButton#titleAction:pressed { background: @hover; }
QFrame#runBlock { background: @runTint; border: 1px solid @runBorder; border-radius: 4px; }
QFrame#runBlock QToolButton { background: transparent; border: none; border-radius: 3px; padding: 0 8px; }
QFrame#runBlock QToolButton#runAction { padding: 0; }
QFrame#runBlock QToolButton:hover, QFrame#runBlock QToolButton:pressed { background: @runHover; }
QFrame#runBlock QToolButton::menu-indicator { image: none; width: 0; }
QFrame#runDivider { background: @runBorder; }
QToolButton#chip { padding: 4px 9px; border-radius: 3px; }
QToolButton#chip:hover { background: @button; }
QToolButton#chip::menu-indicator { image: none; width: 0; }
QStatusBar QToolButton#chip { background: @hover; border: 1px solid @line; }
QStatusBar QToolButton#chip:hover { background: @button; }
QFrame#viewHeader { border: none; border-bottom: 1px solid @line; }
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
QComboBox QAbstractItemView { background: @panel; border: 1px solid @border; selection-background-color: @highlight; outline: 0; }
)");
    const QList<std::pair<QString, QColor>> colors = {
        {"@accentText", theme.accentText},
        {"@accent", theme.accent},
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
        // The side tab of the open panel: a checked flat button in Breeze.
        {"@checked", withAlpha(theme.foreground, 0.125)},
        {"@slate", selectionColor(theme)},
        {"@separator", withAlpha(theme.line, 0.7)},
        // The run block: barely tinted with the accent, so the eye finds it.
        {"@runTint", mixed(theme.window, theme.accent, 0.08)},
        {"@runBorder", mixed(theme.window, theme.accent, 0.2)},
        {"@runHover", mixed(theme.window, theme.accent, 0.18)},
    };
    for (const auto &[name, color] : colors)
        sheet.replace(name, color.name(color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb));

    const QString cross = crossIcon(QStringLiteral("close-%1.svg").arg(theme.name), theme.mutedText);
    const QString crossHover = crossIcon(QStringLiteral("close-%1-hover.svg").arg(theme.name), Qt::white);
    if (!cross.isEmpty() && !crossHover.isEmpty())
        // The margin is taken from the button's own size: AppStyle widens the
        // button by the right margin, and any other margin would shrink the cross.
        sheet += QStringLiteral("QTabBar::close-button { image: url(%1); subcontrol-position: right; border-radius: "
                                "3px; margin: 0 8px 0 0; }\n"
                                "QTabBar::close-button:hover { image: url(%2); background: %3; }\n")
                     .arg(cross, crossHover, theme.highlight.name());

    if (const QString check = checkIcon(QStringLiteral("check-%1.svg").arg(theme.name), Qt::white); !check.isEmpty())
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
    return sheet;
}

QColor selectionColor(const Theme &theme)
{
    return mixed(theme.alternateBase, theme.accent, 0.3);
}

void apply(const Theme &theme)
{
    // Setting either one restyles every widget of the application, even
    // when nothing changes (a window opening on the current theme).
    const QPalette colors = palette(theme);
    if (QApplication::palette() != colors)
        QApplication::setPalette(colors);
    const QString sheet = styleSheet(theme);
    if (qApp->styleSheet() != sheet)
        qApp->setStyleSheet(sheet);
}

} // namespace ThemeStyle
