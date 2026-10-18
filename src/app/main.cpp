#include "MainWindow.h"
#include "git/LibGit2.h"
#include "projects/ProjectConfig.h"
#include "settings/Preferences.h"
#include "ui/BrandMark.h"
#include "welcome/WelcomeWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMenu>
#include <QProxyStyle>
#include <QSettings>
#include <QStyleFactory>
#include <QStyleOption>

namespace {

// Fusion with roomier menus (taller items, wider menu bar entries and a
// margin around the item list of each popup menu) and filled tabs on
// every tab bar.
class AppStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    void polish(QWidget *widget) override
    {
        QProxyStyle::polish(widget);
        // The style sheet rounds the menus: without a transparent window
        // behind them, its square corners show around the rounded border.
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
            return 2;
        case PM_MenuBarItemSpacing:
            return 2;
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
        // Fusion only fills the tabs of a QTabWidget; a standalone tab bar
        // (the mode tabs) would otherwise get hollow tabs.
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

// The settings saved when the editor was called Enlumine, taken over once
// under its new name; those already chosen under the new name stay.
void adoptLegacySettings()
{
    const QString adopted = QStringLiteral("adoptedEnlumineSettings");
    QSettings settings;
    if (settings.value(adopted).toBool())
        return;
    const QSettings legacy(QStringLiteral("Enlumine"), QStringLiteral("Enlumine"));
    const QStringList keys = legacy.allKeys();
    for (const QString &key : keys) {
        if (!settings.contains(key))
            settings.setValue(key, legacy.value(key));
    }
    settings.setValue(adopted, true);
}

void showWelcome();

// A main window on `folder`; Close Project brings the welcome window back.
void openProject(const QString &folder)
{
    auto *window = new MainWindow(folder);
    window->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(window, &MainWindow::projectClosed, [] { showWelcome(); });
    window->show();
}

// The projects opened before; the one chosen opens in a main window.
void showWelcome()
{
    auto *welcome = new WelcomeWindow;
    welcome->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(welcome, &WelcomeWindow::projectChosen, welcome, [welcome](const ProjectConfig &project) {
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
    QApplication app(argc, argv);
    // libgit2 stays initialized for the whole run: each read (change marks
    // while typing, file statuses) would otherwise set up and tear down its
    // global state.
    const libgit2::Session git;
    QApplication::setApplicationName(QStringLiteral("Duet"));
    QApplication::setOrganizationName(QStringLiteral("Duet"));
    QApplication::setApplicationVersion(QStringLiteral(DUET_VERSION));
    adoptLegacySettings();
    // Fusion follows the application palette on every system, which
    // enables the dark theme everywhere.
    QApplication::setStyle(new AppStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
    // Inter for the whole interface (embedded), at the system's size unless
    // Preferences ▸ Appearance sets one.
    QApplication::setFont(Preferences::load().interfaceFont());
    // Every window's icon; on Wayland, the desktop file's icon.
    QApplication::setWindowIcon(BrandMark::windowIcon());
    QGuiApplication::setDesktopFileName(QStringLiteral("duet"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Source code editor with an integrated Claude terminal"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("folder"), QStringLiteral("Project folder to open"));
    parser.process(app);

    // A folder given: straight to it, set up from its files if it is new.
    const QStringList arguments = parser.positionalArguments();
    if (arguments.isEmpty())
        showWelcome();
    else
        openProject(QDir(arguments.first()).absolutePath());
    return app.exec();
}
