// SPDX-License-Identifier: GPL-3.0-only

#include "FileOpenRouter.h"
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
#include "ui/DesktopIntegration.h"
#include "ui/Motion.h"
#include "ui/StrayWindowCheck.h"
#include "ui/ToolTipPlacement.h"
#include "ui/UiMetrics.h"
#include "welcome/WelcomeWindow.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>

#include <algorithm>

namespace {

constexpr int closeButtonWidth = 20 + 8;
// The arrows of a spin box, for an 11 pt interface font: Fusion's 14 px are
// hard to hit with the mouse.
constexpr int spinBoxButtonWidth = 22;

// `fusionRect`, a part of a spin box laid out by Fusion, with buttons wider
// by `extra`: the edit field gives them the room.
QRect widenSpinBoxButtons(QRect fusionRect, QStyle::SubControl part, int extra, Qt::LayoutDirection direction)
{
    const bool buttonsOnRight = direction == Qt::LeftToRight;
    switch (part) {
    case QStyle::SC_SpinBoxUp:
    case QStyle::SC_SpinBoxDown:
        return buttonsOnRight ? fusionRect.adjusted(-extra, 0, 0, 0) : fusionRect.adjusted(0, 0, extra, 0);
    case QStyle::SC_SpinBoxEditField:
        return buttonsOnRight ? fusionRect.adjusted(0, 0, -extra, 0) : fusionRect.adjusted(extra, 0, 0, 0);
    default:
        return fusionRect;
    }
}

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
        case CT_SpinBox:
            if (const auto *spinBox = qstyleoption_cast<const QStyleOptionSpinBox *>(option))
                result.rwidth() += spinBoxExtraWidth(spinBox, widget);
            break;
        default:
            break;
        }
        return result;
    }

    QRect subControlRect(ComplexControl control,
                         const QStyleOptionComplex *option,
                         SubControl part,
                         const QWidget *widget) const override
    {
        const QRect rect = QProxyStyle::subControlRect(control, option, part, widget);
        const auto *spinBox = qstyleoption_cast<const QStyleOptionSpinBox *>(option);
        if (control != CC_SpinBox || !spinBox)
            return rect;
        return widenSpinBoxButtons(rect, part, spinBoxExtraWidth(spinBox, widget), spinBox->direction);
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

private:
    // How much wider than Fusion's the buttons of `spinBox` are drawn.
    int spinBoxExtraWidth(const QStyleOptionSpinBox *spinBox, const QWidget *widget) const
    {
        if (spinBox->buttonSymbols == QAbstractSpinBox::NoButtons)
            return 0;
        const int fusionWidth = QProxyStyle::subControlRect(CC_SpinBox, spinBox, SC_SpinBoxUp, widget).width();
        return std::max(0, UiMetrics::scaled(spinBoxButtonWidth) - fusionWidth);
    }
};

// Kodamiko's downloads, from its application data to ~/.kodamiko/tools, once.
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

// A folder the system asks this process to open while it has no project
// window: it takes the welcome window's place.
void openInPlaceOfWelcome(const QString &folder)
{
    openProject(folder);
    const QWidgetList windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
        if (auto *welcome = qobject_cast<WelcomeWindow *>(window))
            welcome->close();
    }
}

} // namespace

int main(int argc, char *argv[])
{
    // Required by Qt WebEngine (Markdown preview), before the application exists.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    PreviewSite::registerScheme();
    const QApplication app(argc, argv);
    DesktopIntegration::identifyApplication();
    ClickableCursors::install();
    StrayWindowCheck::install();

    const libgit2::Session git;
    QApplication::setApplicationName(QStringLiteral("Kodamiko"));
    QApplication::setOrganizationName(QStringLiteral("Kodamiko"));
    QApplication::setApplicationVersion(QStringLiteral(KDK_VERSION));
    LogFiles::install(QStringLiteral("kodamiko"));
    adoptLegacyToolchains();
    Motion::policy().setEnabled(Preferences::load().animations);
    Motion::install();
    ToolTipPlacement::install();

    QApplication::setStyle(new AppStyle(QStyleFactory::create(QStringLiteral("Fusion"))));

    QApplication::setFont(Preferences::load().interfaceFont());

    QApplication::setWindowIcon(BrandMark::windowIcon(Theme::named(Preferences::load().theme)));
    QGuiApplication::setDesktopFileName(QStringLiteral("kodamiko"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Source code editor with an integrated Claude terminal"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("folder"), QStringLiteral("Project folder to open"));
    parser.process(app);
    new FileOpenRouter(openInPlaceOfWelcome, qApp);

    const QStringList arguments = parser.positionalArguments();
    if (arguments.isEmpty()) {
        showWelcome();
        return QApplication::exec();
    }
    const QString folder = QDir(arguments.first()).absolutePath();
    if (!QFileInfo(folder).isDir()) {
        // A file or a mistyped path would open as a broken project, and be
        // remembered among the recent ones.
        qWarning("Kodamiko opens folders: %s is not one", qPrintable(QDir::toNativeSeparators(folder)));
        showWelcome();
        return QApplication::exec();
    }
    // Already open in another process: its window comes forward instead.
    if (ProjectWindows::raise(folder))
        return 0;
    openProject(folder);
    return QApplication::exec();
}
