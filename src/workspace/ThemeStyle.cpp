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
    const QString path = folder + QLatin1Char('/') + name;
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
    return writeIcon(name, QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>"
                                          "<path d='M4.5 4.5l7 7M11.5 4.5l-7 7' stroke='%1' stroke-width='1.6' "
                                          "stroke-linecap='round'/></svg>")
                               .arg(color.name()));
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
QToolTip { color: @text; background: @button; border: 1px solid @border; border-radius: 6px; padding: 4px 6px; }
QMenuBar { background: @window; border-bottom: 1px solid @line; }
QMenuBar::item { padding: 4px 10px; border-radius: 5px; background: transparent; }
QMenuBar::item:selected { background: @button; color: @accentText; }
QMenu { background: @panel; border: 1px solid @border; border-radius: 8px; padding: 5px; }
QMenu::item { padding: 5px 28px 5px 22px; border-radius: 5px; }
QMenu::item:selected { color: white; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @grad1, stop:1 @grad2); }
QMenu::item:disabled { color: @muted; }
QMenu::separator { height: 1px; background: @line; margin: 4px 10px; }
QLineEdit, QPlainTextEdit, QTextBrowser { background: @base; border: 1px solid @border; border-radius: 6px; padding: 3px 6px; selection-background-color: @highlight; }
QLineEdit:focus, QPlainTextEdit:focus { border: 1px solid @accent; }
QPushButton { background: @button; border: 1px solid @border; border-radius: 6px; padding: 4px 14px; }
QPushButton:hover { border-color: @accent; }
QPushButton:pressed { background: @highlight; }
QPushButton:default { color: white; border: none; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @grad1, stop:1 @grad2); }
QPushButton:disabled { color: @muted; border-color: @line; }
QToolButton { border-radius: 5px; padding: 2px 5px; }
QToolButton:hover { background: @button; }
QToolButton:checked { background: @selection; color: @accentText; }
QTabWidget::pane { border: none; }
QTabBar::tab { background: transparent; color: @muted; padding: 5px 14px; border: none; }
QTabBar::tab:hover { color: @text; background: @panel; }
QTabBar::tab:selected { color: @accentText; background: @base; border-bottom: 2px solid @accent; }
QTabBar::tab:bottom:selected { border-bottom: none; border-top: 2px solid @accent; }
QWidget#sideStrip { background: @window; border-right: 1px solid @line; }
QTabBar#sideTabs { background: transparent; }
QTabBar#sideTabs::tab { padding: 14px 6px; margin: 2px 3px; border-radius: 6px; color: @muted; }
QTabBar#sideTabs::tab:hover { background: @hover; color: @text; }
QTabBar#sideTabs::tab:selected { background: @selection; color: @accentText; border: none; }
QHeaderView::section { background: @panel; color: @muted; border: none; border-bottom: 1px solid @line; padding: 4px 6px; }
QTreeView, QTreeWidget, QListView { background: @base; border: none; outline: 0; }
QTreeView::item:selected, QTreeWidget::item:selected { background: @selection; color: @text; }
QTreeView::item:selected:active, QTreeWidget::item:selected:active { color: white; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @grad1, stop:1 @grad2); }
QTreeView::item:hover { background: @hover; }
QSplitter::handle { background: @window; }
QFrame#card { background: @panel; border: 1px solid @line; border-radius: 10px; }
QFrame#card QPlainTextEdit { background: @base; }
QStatusBar { background: transparent; border-top: 1px solid @line; color: @muted; }
TitleBar QMenuBar { background: transparent; border: none; }
QToolButton#chip { padding: 4px 9px; border-radius: 6px; }
QToolButton#chip:hover { background: @button; }
QToolButton#chip::menu-indicator { image: none; width: 0; }
QStatusBar::item { border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:vertical { background: @border; border-radius: 4px; min-height: 28px; margin: 2px; }
QScrollBar::handle:horizontal { background: @border; border-radius: 4px; min-width: 28px; margin: 2px; }
QScrollBar::handle:hover { background: @highlight; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QCheckBox::indicator:checked { background: @accent; border: 1px solid @accent; border-radius: 3px; }
QCheckBox::indicator:unchecked { background: @base; border: 1px solid @border; border-radius: 3px; }
QComboBox { background: @button; border: 1px solid @border; border-radius: 6px; padding: 2px 8px; }
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
        {"@grad1", theme.gradientStart},
        {"@grad2", theme.gradientEnd},
    };
    for (const auto &[name, color] : colors)
        sheet.replace(name, color.name());

    const QString cross = crossIcon(QStringLiteral("close-%1.svg").arg(theme.name), theme.mutedText);
    const QString crossHover = crossIcon(QStringLiteral("close-%1-hover.svg").arg(theme.name), Qt::white);
    if (!cross.isEmpty() && !crossHover.isEmpty())
        sheet += QStringLiteral("QTabBar::close-button { image: url(%1); subcontrol-position: right; border-radius: 4px; margin: 1px; }\n"
                                "QTabBar::close-button:hover { image: url(%2); background: %3; }\n")
                     .arg(cross, crossHover, theme.highlight.name());
    return sheet;
}

void apply(const Theme &theme)
{
    QApplication::setPalette(palette(theme));
    qApp->setStyleSheet(styleSheet(theme));
}

} // namespace ThemeStyle
