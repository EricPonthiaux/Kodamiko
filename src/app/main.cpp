// SPDX-License-Identifier: GPL-3.0-only

#include "MainWindow.h"
#include "editor/PreviewSite.h"
#include "git/LibGit2.h"
#include "log/LogFiles.h"
#include "projects/ProjectConfig.h"
#include "projects/ProjectWindowServer.h"
#include "settings/Preferences.h"
#include "toolchains/Toolchains.h"
#include "ui/BrandMark.h"
#include "ui/ClickableCursors.h"
#include "ui/Motion.h"
#include "ui/StrayWindowCheck.h"
#include "ui/ToolTipPlacement.h"
#include "ui/UiMetrics.h"
#include "welcome/WelcomeWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QProxyStyle>
#include <QSettings>
#include <QStyleFactory>
#include <QStyleOption>

namespace {

constexpr int closeButtonWidth = 20 + 8;

class AppStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    int styleHint(StyleHint hint,
                  const QStyleOption *option,
                  const QWidget *widget,
                  QStyleHintReturn *data) const override
    {
        if (hint == SH_Widget_Animation_Duration && !Motion::policy().isEnabled())
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, data);
    }

    void polish(QWidget *widget) override
    {
        QProxyStyle::polish(widget);

        if (qobject_cast<QMenu *>(widget)) {
            widget->setAttribute(Qt::WA_TranslucentBackground);
            widget->setWindowFlag(Qt::FramelessWindowHint);
            widget->setWindowFlag(Qt::NoDropShadowWindowHint);
        }
    }

    int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override
    {
        switch (metric) {
        case PM_MenuHMargin:
            return 4;
        case PM_MenuVMargin:
            return 6;
        case PM_MenuBarHMargin:
            return 4;
        case PM_MenuBarVMargin:
        case PM_MenuBarItemSpacing:
            return 2;
        case PM_TabCloseIndicatorWidth:
            return closeButtonWidth;
        // The icons that no widget sizes itself grow with the text.
        case PM_SmallIconSize:
        case PM_ButtonIconSize:
        case PM_TabBarIconSize:
        case PM_ListViewIconSize:
            return UiMetrics::scaled(UiMetrics::iconSize);
        default:
            return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }

    QSize sizeFromContents(ContentsType type,
                           const QStyleOption *option,
                           const QSize &size,
                           const QWidget *widget) const override
    {
        QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
        switch (type) {
        case CT_MenuItem: {
            const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
            if (item && item->menuItemType == QStyleOptionMenuItem::Separator)
                result.rheight() += 4;
            else
                result += QSize(24, 8);
            break;
        }
        case CT_MenuBarItem:
            result += QSize(12, 6);
            break;
        default:
            break;
        }
        return result;
    }

    void drawControl(ControlElement element,
                     const QStyleOption *option,
                     QPainter *painter,
                     const QWidget *widget) const override
    {
        if (element == CE_TabBarTabShape) {
            if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
                tab && !(tab->features & QStyleOptionTab::HasFrame)) {
                QStyleOptionTab framed(*tab);
                framed.features |= QStyleOptionTab::HasFrame;
                QProxyStyle::drawControl(element, &framed, painter, widget);
                return;
            }
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }
};

void adoptLegacySettings()
{
    const QString adopted = QStringLiteral("adoptedDuetSettings");
    QSettings settings;
    if (settings.value(adopted).toBool())
        return;
    const QSettings legacy(QStringLiteral("Duet"), QStringLiteral("Duet"));
    const QStringList keys = legacy.allKeys();
    for (const QString &key : keys) {
        if (!settings.contains(key))
            settings.setValue(key, legacy.value(key));
    }
    settings.setValue(adopted, true);
}

// Duet's downloads, from its application data to ~/.duet/tools, once.
void adoptLegacyToolchains()
{
    const QString legacy = LegacyToolchains::defaultRoot();
    if (!QFileInfo::exists(legacy))
        return;
    const QStringList unmoved = LegacyToolchains::moveInto(legacy, Toolchains::defaultRoot());
    if (!unmoved.isEmpty())
        qWarning("Could not move these downloads to %s, they will be downloaded again: %s",
                 qPrintable(QDir::toNativeSeparators(Toolchains::defaultRoot())),
                 qPrintable(unmoved.join(QStringLiteral(", "))));
}

void showWelcome();

// A main window on `folder`; Close Project brings the welcome window back.
void openProject(const QString &folder)
{
    auto *window = new MainWindow(folder);
    window->setAttribute(Qt::WA_DeleteOnClose);
    StrayWindowCheck::allow(window);
    QObject::connect(window, &MainWindow::projectClosed, [] { showWelcome(); });
    window->show();
}

// The projects opened before; the one chosen opens in a main window.
void showWelcome()
{
    auto *welcome = new WelcomeWindow;
    welcome->setAttribute(Qt::WA_DeleteOnClose);
    StrayWindowCheck::allow(welcome);
    QObject::connect(welcome, &WelcomeWindow::projectChosen, welcome, [welcome](const ProjectConfig &project) {
        if (!ProjectWindows::raise(project.path))
            openProject(project.path);
        welcome->close();
    });
    welcome->show();
}

} // namespace

int main(int argc, char *argv[])
{
    // Required by Qt WebEngine (Markdown preview), before the application exists.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    PreviewSite::registerScheme();
    const QApplication app(argc, argv);
    ClickableCursors::install();
    StrayWindowCheck::install();

    const libgit2::Session git;
    QApplication::setApplicationName(QStringLiteral("Duet"));
    QApplication::setOrganizationName(QStringLiteral("Duet"));
    QApplication::setApplicationVersion(QStringLiteral(DUET_VERSION));
    LogFiles::install(QStringLiteral("duet"));
    adoptLegacySettings();
    adoptLegacyToolchains();
    Motion::policy().setEnabled(Preferences::load().animations);
    Motion::install();
    ToolTipPlacement::install();

    QApplication::setStyle(new AppStyle(QStyleFactory::create(QStringLiteral("Fusion"))));

    QApplication::setFont(Preferences::load().interfaceFont());

    QApplication::setWindowIcon(BrandMark::windowIcon());
    QGuiApplication::setDesktopFileName(QStringLiteral("duet"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Source code editor with an integrated Claude terminal"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("folder"), QStringLiteral("Project folder to open"));
    parser.process(app);

    const QStringList arguments = parser.positionalArguments();
    if (arguments.isEmpty()) {
        showWelcome();
        return QApplication::exec();
    }
    const QString folder = QDir(arguments.first()).absolutePath();
    // Already open in another process: its window comes forward instead.
    if (ProjectWindows::raise(folder))
        return 0;
    openProject(folder);
    return QApplication::exec();
}
