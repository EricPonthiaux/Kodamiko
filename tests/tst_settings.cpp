#include "settings/Keymap.h"
#include "settings/Preferences.h"
#include "settings/PreferencesPopup.h"
#include "syntax/ScintillaStyles.h"
#include "ui/AppFonts.h"
#include "ui/Theme.h"

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QTreeWidget>
#include <Qsci/qsciscintilla.h>
#include <QtTest>

class TestSettings : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // Settings of the tests, not the user's.
        QCoreApplication::setOrganizationName(QStringLiteral("DuetTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_settings"));
        QSettings().clear();
    }

    void cleanup() { QSettings().clear(); }

    void preferencesRoundTrip()
    {
        Preferences preferences;
        QCOMPARE(Preferences::load(), preferences); // the defaults
        preferences.theme = QStringLiteral("light");
        preferences.codeFontSize = 14;
        preferences.interfaceFontSize = 12;
        preferences.treeFontSize = 15;
        preferences.codeFontWeight = QFont::Light;
        preferences.codeLineHeight = 1.6;
        preferences.ligatures = true;
        preferences.editor.wrapLines = true;
        preferences.editor.codeFolding = false;
        preferences.claudeNotifications = false;
        preferences.save();
        QCOMPARE(Preferences::load(), preferences);
        // The menu commands' keys.
        QCOMPARE(QSettings().value("claudeNotifications").toBool(), false);
        QCOMPARE(QSettings().value("theme").toString(), QString("light"));
    }

    void fontSizesHaveDefaults()
    {
        Preferences preferences;
        preferences.codeFontSize = 30;
        // The tree follows the code font, never below the interface's.
        QCOMPARE(preferences.treeFont().pointSize(), 30);
        preferences.interfaceFontSize = 9;
        preferences.codeFontSize = 7;
        QCOMPARE(preferences.interfaceFont().pointSize(), 9);
        QCOMPARE(preferences.treeFont().pointSize(), 9);
        preferences.treeFontSize = 14;
        QCOMPARE(preferences.treeFont().pointSize(), 14);
    }

    void typographyReachesTheTheme()
    {
        Preferences preferences;
        preferences.codeFontWeight = QFont::Light;
        preferences.codeLineHeight = 1.8;
        preferences.ligatures = true;
        Theme theme = Theme::dark();
        preferences.applyTypography(theme);
        QCOMPARE(theme.font.weight(), QFont::Light);
        QCOMPARE(theme.lineHeight, 1.8);
        QVERIFY(theme.editorLigatures);
        // The theme's font itself has none: the terminal uses it.
        QVERIFY(AppFonts::isBuiltInCode(theme.font.family()));
        QCOMPARE(AppFonts::withLigatures(theme.font, false).family(), theme.font.family());
        QVERIFY(AppFonts::withLigatures(theme.font, true).family() != theme.font.family());
        QVERIFY(AppFonts::scintillaFamily(theme.font, true).endsWith("#liga"));
        QVERIFY(!AppFonts::scintillaFamily(theme.font, false).contains('#'));
    }

    // Through QScintilla, as the editor draws: ligatures, weight and line
    // height each change what is drawn.
    void editorDrawsTheTypography()
    {
        QsciScintilla editor;
        editor.resize(400, 120);
        editor.setText("a -> b != c\nnext line");
        editor.setCaretWidth(0);
        editor.setMarginWidth(1, 0);
        editor.show();
        const auto draw = [&editor](const Preferences &preferences) {
            Theme theme = Theme::dark();
            preferences.applyTypography(theme);
            ScintillaStyles::applyDefaultStyle(editor, theme);
            ScintillaStyles::applyTypography(editor, theme.font, theme.lineHeight);
            return editor.viewport()->grab().toImage();
        };
        Preferences preferences;
        const QImage plain = draw(preferences);
        const long plainHeight = editor.SendScintilla(QsciScintillaBase::SCI_TEXTHEIGHT, 0);
        preferences.ligatures = true;
        QVERIFY(draw(preferences) != plain);
        preferences.ligatures = false;
        QCOMPARE(draw(preferences), plain);
        // Without anything to join, the same letters: the family behind
        // "#liga" is found, not a fallback.
        editor.setText("abc def\nnext line");
        const QImage letters = draw(preferences);
        preferences.ligatures = true;
        QCOMPARE(draw(preferences), letters);
        preferences.ligatures = false;
        editor.setText("a -> b != c\nnext line");
        preferences.codeFontWeight = QFont::Light;
        QVERIFY(draw(preferences) != plain);
        preferences.codeFontWeight = QFont::Normal;
        preferences.codeLineHeight = 2.0;
        draw(preferences);
        QVERIFY(editor.SendScintilla(QsciScintillaBase::SCI_TEXTHEIGHT, 0) > plainHeight);
    }

    void oldDarkThemeSettingIsRead()
    {
        QSettings().setValue("darkTheme", false);
        QCOMPARE(Preferences::load().theme, QString("light"));
    }

    void keymapListsMenuCommands()
    {
        QMenuBar bar;
        Menus menus(bar);
        const QList<Keymap::Command> commands = Keymap::commands(&bar);
        QCOMPARE(commands.size(), 3);
        QVERIFY(commands[2].defaultShortcuts.isEmpty());
        QCOMPARE(commands[0].id, QString("Edit/Find"));
        QCOMPARE(commands[1].id, QString("Edit/Folding/Fold Block"));
        QCOMPARE(commands[1].title, QString("Edit ▸ Folding ▸ Fold Block"));
        QCOMPARE(commands[1].defaultShortcuts, QList<QKeySequence>{QKeySequence("Ctrl+-")});

        Keymap::apply(commands, {{"Edit/Find", {QKeySequence("Ctrl+K")}}, {"Edit/Folding/Fold Block", {}}});
        QCOMPARE(menus.find->shortcut(), QKeySequence("Ctrl+K"));
        QVERIFY(menus.fold->shortcut().isEmpty());
        // The defaults are those of the first listing.
        Keymap::apply(Keymap::commands(&bar), {});
        QCOMPARE(menus.find->shortcut(), QKeySequence("Ctrl+F"));
        QCOMPARE(menus.fold->shortcut(), QKeySequence("Ctrl+-"));
    }

    void keymapOverridesAreSaved()
    {
        const Keymap::Overrides overrides = {{"Edit/Find", {QKeySequence("Ctrl+K, Ctrl+F")}}, {"Edit/Replace", {}}};
        Keymap::saveOverrides(overrides);
        QCOMPARE(Keymap::loadOverrides(), overrides);
        Keymap::saveOverrides({});
        QVERIFY(!QSettings().contains("keymap"));
    }

    void dialogReturnsItsSettings()
    {
        Preferences preferences;
        preferences.codeFontSize = 13;
        preferences.interfaceFontSize = 10;
        preferences.editor.showWhitespace = true;
        preferences.snapshotBeforePrompt = false;
        preferences.codeFontWeight = QFont::Medium;
        preferences.codeLineHeight = 1.75;
        preferences.ligatures = true;
        QWidget window;
        PreferencesPopup popup(Theme::dark(), &window);
        popup.load(preferences, {}, {});
        QCOMPARE(popup.preferences(), preferences);
        preferences.theme = "light";
        popup.load(preferences, {}, {}); // reopened with other settings
        QCOMPARE(popup.preferences(), preferences);
    }

    void dialogMovesAShortcutToTheSelectedCommand()
    {
        QMenuBar bar;
        Menus menus(bar);
        QWidget window;
        PreferencesPopup dialog(Theme::dark(), &window);
        dialog.load(Preferences(), Keymap::commands(&bar), {});
        QCOMPARE(dialog.commandList()->topLevelItemCount(), 3);

        dialog.selectCommand("Edit/Find");
        dialog.assignShortcut(QKeySequence("Ctrl+-"));
        const Keymap::Overrides expected = {{"Edit/Find", {QKeySequence("Ctrl+-")}}, {"Edit/Folding/Fold Block", {}}};
        QCOMPARE(dialog.shortcutOverrides(), expected);
        QVERIFY(dialog.keymapNote()->text().contains("Fold Block"));
        QCOMPARE(dialog.commandList()->topLevelItem(1)->text(1), QString());

        // Back to the default: no override left for it.
        dialog.assignShortcut(QKeySequence("Ctrl+F"));
        QVERIFY(!dialog.shortcutOverrides().contains("Edit/Find"));
    }

private:
    // Edit ▸ Find (Ctrl+F), Edit ▸ Folding ▸ Fold Block (Ctrl+-) and Fold All.
    struct Menus {
        explicit Menus(QMenuBar &bar)
        {
            QMenu *edit = bar.addMenu("&Edit");
            find = edit->addAction("&Find…");
            find->setShortcut(QKeySequence("Ctrl+F"));
            edit->addSeparator();
            QMenu *folding = edit->addMenu("Fol&ding");
            fold = folding->addAction("&Fold Block");
            fold->setShortcut(QKeySequence("Ctrl+-"));
            folding->addAction("Fold &All"); // no shortcut
        }

        QAction *find;
        QAction *fold;
    };
};

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
