// SPDX-License-Identifier: GPL-3.0-only

#include "settings/FontSettings.h"
#include "settings/Keymap.h"
#include "settings/Preferences.h"
#include "settings/PreferencesPopup.h"
#include "ui/AppFonts.h"
#include "ui/Theme.h"
#include "ui/ThemeStyle.h"
#include "ui/TitleBar.h"
#include "ui/TreeMetrics.h"
#include "ui/UiMetrics.h"
#include "workspace/FileTreeView.h"
#include "workspace/FilesPanel.h"

#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QTabBar>
#include <QToolButton>
#include <QTreeView>
#include <QWheelEvent>
#include <QtTest>

#include <cstdlib>

namespace {

// The parts of the main window whose fonts Preferences ▸ Fonts sets,
// under the theme's style sheet as in the application.
struct Window {
    QWidget root;
    TitleBar *titleBar = new TitleBar(&root);
    QMenu *menu = titleBar->menuBar()->addMenu(QStringLiteral("&File"));
    FilesPanel *files = new FilesPanel(&root);
    QTreeView *outline = new QTreeView(&root);
    QTabBar *tabs = new QTabBar(&root);
    QLabel *label = new QLabel(QStringLiteral("Changes"), new QWidget(&root));
    QLabel *title = new QLabel(QStringLiteral("History"), &root); // 1.15 × the interface font
    QLabel *code = new QLabel(QStringLiteral("int main()"), &root);

    Window()
    {
        menu->addAction(QStringLiteral("Open"));
        tabs->addTab(QStringLiteral("main.cpp"));
        AppFonts::setInterfaceFont(title, 1.15);
        code->setFont(AppFonts::codeFont(17));
        FontSettings::applyTreeFont(Preferences::load().treeFont(), &root);
        root.show();
    }

    QToolButton *projectChip() const { return titleBar->findChild<QToolButton *>(QStringLiteral("projectChip")); }

    QToolButton *viewSelector() const { return files->findChild<QToolButton *>(QStringLiteral("viewSelector")); }

    QFrame *panelHeader() const { return files->findChild<QFrame *>(QStringLiteral("viewHeader")); }

    // Every widget that shows the interface font as is.
    QList<QWidget *> interfaceWidgets() const
    {
        return {titleBar->menuBar(), menu, projectChip(), viewSelector(), tabs, label};
    }
};

Preferences withSizes(int interfaceSize, int treeSize)
{
    Preferences preferences;
    preferences.interfaceFontSize = interfaceSize;
    preferences.treeFontSize = treeSize;
    return preferences;
}

// Applies `preferences` as File ▸ Preferences ▸ Apply does.
void applyPreferences(const Preferences &preferences, const Window &window)
{
    const Preferences previous = Preferences::load();
    preferences.save();
    FontSettings::apply(preferences, previous, &window.root);
}

// `notches` of the wheel over `tree`: up when positive, down otherwise.
void turnWheel(const QTreeView *tree, int notches, Qt::KeyboardModifiers modifiers)
{
    constexpr int notch = 120;
    const QPointF center = QRectF(tree->viewport()->rect()).center();
    for (int turned = 0; turned < std::abs(notches); ++turned) {
        QWheelEvent event(center, tree->viewport()->mapToGlobal(center), {}, QPoint(0, notches > 0 ? notch : -notch),
                          Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
        QApplication::sendEvent(tree->viewport(), &event);
    }
}

} // namespace

class TestFontSettings : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // Settings of the tests, not the user's.
        QCoreApplication::setOrganizationName(QStringLiteral("DuetTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_fontsettings"));
    }

    void init()
    {
        QSettings().clear();
        QApplication::setFont(Preferences().interfaceFont());
        ThemeStyle::apply(Theme::dark());
    }

    void cleanup() { QSettings().clear(); }

    void interfaceFontReachesEveryInterfaceWidget()
    {
        const Window window;

        applyPreferences(withSizes(15, 0), window);

        for (const QWidget *widget : window.interfaceWidgets())
            QVERIFY2(widget->font().pointSize() == 15, qPrintable(widget->metaObject()->className()));
        QCOMPARE(window.title->font().pointSizeF(), 15 * 1.15);
    }

    void interfaceFontLeavesTheCodeFont()
    {
        const Window window;

        applyPreferences(withSizes(15, 0), window);

        QCOMPARE(window.code->font().pointSize(), 17);
        QVERIFY(AppFonts::isBuiltInCode(window.code->font().family()));
    }

    void interfaceFontGoesBackDown()
    {
        const Window window;
        applyPreferences(withSizes(16, 0), window);

        applyPreferences(withSizes(9, 0), window);

        for (const QWidget *widget : window.interfaceWidgets())
            QVERIFY2(widget->font().pointSize() == 9, qPrintable(widget->metaObject()->className()));
    }

    void barsGrowWithTheInterfaceFont()
    {
        const Window window;

        applyPreferences(withSizes(2 * AppFonts::defaultInterfaceSize, 0), window);

        QCOMPARE(window.titleBar->height(), 2 * UiMetrics::titleBarHeight);
        QCOMPARE(window.panelHeader()->height(), 2 * UiMetrics::panelHeaderHeight);
        // Taller, but no taller than a tab.
        QVERIFY(window.projectChip()->height() > UiMetrics::toolbarButtonSize);
        QCOMPARE(window.projectChip()->height(), UiMetrics::controlHeight());
        QVERIFY(window.projectChip()->height() <= UiMetrics::tabHeight());
        QCOMPARE(window.projectChip()->iconSize().width(), 2 * UiMetrics::chipIconSize);
    }

    void buttonIconsGrowWithTheInterfaceFont()
    {
        const Window window;

        applyPreferences(withSizes(2 * AppFonts::defaultInterfaceSize, 0), window);

        const QToolButton *collapse = nullptr;
        for (const QToolButton *button : window.files->findChildren<QToolButton *>()) {
            if (button->toolTip() == QStringLiteral("Collapse All"))
                collapse = button;
        }
        QVERIFY(collapse);
        QCOMPARE(collapse->iconSize().width(), 2 * UiMetrics::iconSize);
        QVERIFY(collapse->width() > collapse->iconSize().width());
    }

    void treeFontReachesEveryTree()
    {
        const Window window;

        applyPreferences(withSizes(11, 16), window);

        for (const QTreeView *tree : {static_cast<QTreeView *>(window.files->tree()), window.outline}) {
            QCOMPARE(tree->font().pointSize(), 16);
            QCOMPARE(tree->iconSize().height(), QFontMetrics(tree->font()).height());
        }
    }

    void treesFollowTheCodeFontSizeByDefault()
    {
        const Window window;
        Preferences preferences = withSizes(11, 0);
        preferences.codeFontSize = 14;

        applyPreferences(preferences, window);

        QCOMPARE(window.outline->font().pointSize(), 14);
        QCOMPARE(window.files->tree()->font().pointSize(), 14);
    }

    void treesKeepTheirFontWhenOnlyTheInterfaceChanges()
    {
        const Window window;
        applyPreferences(withSizes(11, 16), window);

        applyPreferences(withSizes(13, 16), window);

        QCOMPARE(window.outline->font().pointSize(), 16);
        QCOMPARE(window.files->tree()->font().pointSize(), 16);
    }

    void fontsSurviveAThemeChange()
    {
        const Window window;
        applyPreferences(withSizes(14, 16), window);

        ThemeStyle::apply(Theme::light());

        QCOMPARE(window.outline->font().pointSize(), 16);
        QCOMPARE(window.files->tree()->font().pointSize(), 16);
        QCOMPARE(window.code->font().pointSize(), 17);
        for (const QWidget *widget : window.interfaceWidgets())
            QVERIFY2(widget->font().pointSize() == 14, qPrintable(widget->metaObject()->className()));
    }

    void platformClassFontsGiveWayToTheInterfaceFont()
    {
        const Window window;
        applyPreferences(withSizes(14, 16), window);
        // As KDE's platform theme gives menu bars and item views its font.
        QFont platformFont = QApplication::font();
        platformFont.setPointSize(7);
        QApplication::setFont(platformFont, "QMenuBar");

        ThemeStyle::apply(Theme::light());

        QCOMPARE(window.titleBar->menuBar()->font().pointSize(), 14);
    }

    void dialogSizesAreSavedAndApplied()
    {
        Window window;
        PreferencesPopup popup(Theme::dark(), &window.root);
        popup.load(withSizes(15, 17), {}, {});

        applyPreferences(popup.preferences(), window);

        QCOMPARE(Preferences::load().interfaceFontSize, 15);
        QCOMPARE(Preferences::load().treeFontSize, 17);
        QCOMPARE(window.titleBar->menuBar()->font().pointSize(), 15);
        QCOMPARE(window.outline->font().pointSize(), 17);
    }

    void ctrlWheelZoomsOnlyTheTreeUnderTheMouse()
    {
        const Window window;
        applyPreferences(withSizes(11, 14), window);

        turnWheel(window.outline, 2, Qt::ControlModifier);

        QCOMPARE(window.outline->font().pointSize(), 16);
        QCOMPARE(window.outline->iconSize().height(), QFontMetrics(window.outline->font()).height());
        QCOMPARE(window.files->tree()->font().pointSize(), 14);
    }

    void wheelWithoutCtrlKeepsTheTreeFont()
    {
        const Window window;
        applyPreferences(withSizes(11, 14), window);

        turnWheel(window.outline, 1, Qt::NoModifier);

        QCOMPARE(window.outline->font().pointSize(), 14);
    }

    void treeZoomStopsAtTheLargestFontSize()
    {
        const Window window;
        applyPreferences(withSizes(11, 14), window);

        turnWheel(window.outline, Preferences::maximumFontSize, Qt::ControlModifier);
        turnWheel(window.outline, 1, Qt::ControlModifier);

        QCOMPARE(window.outline->font().pointSize(), Preferences::maximumFontSize);
    }

    void treeZoomSurvivesATreeFontChange()
    {
        const Window window;
        applyPreferences(withSizes(11, 14), window);
        turnWheel(window.outline, 1, Qt::ControlModifier);

        applyPreferences(withSizes(11, 16), window);

        QCOMPARE(window.outline->font().pointSize(), 17);
        QCOMPARE(window.files->tree()->font().pointSize(), 16);
    }

    void defaultInterfaceSizeIsEleven()
    {
        QCOMPARE(Preferences().interfaceFont().pointSize(), 11);
        QSettings().setValue(QStringLiteral("interfaceFont/size"), 0); // "System", before
        QCOMPARE(Preferences::load().interfaceFontSize, 11);
    }
};

QTEST_MAIN(TestFontSettings)
#include "tst_fontsettings.moc"
