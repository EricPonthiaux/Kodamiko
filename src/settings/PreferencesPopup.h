// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Keymap.h"
#include "Preferences.h"
#include "ui/OverlayPopup.h"
#include "ui/Theme.h"

class QAbstractButton;
class QButtonGroup;
class QComboBox;
class QDoubleSpinBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTreeWidget;
class PopupHeader;
class QCheckBox;
class TerminalPreferencesPage;

// File ▸ Preferences, a popup over the window like Find in Files: a side
// bar of pages (Appearance, Fonts, Editor, Files, Claude, Terminal,
// Keymap), each made of
// cards of settings. Changes apply on OK or Apply; Cancel, Escape or a
// click elsewhere leave everything as it was.
class PreferencesPopup : public OverlayPopup {
    Q_OBJECT

public:
    explicit PreferencesPopup(const Theme &theme, QWidget *parent);

    // Shows the popup with these settings.
    void popup(const Preferences &preferences,
               const QList<Keymap::Command> &commands,
               const Keymap::Overrides &overrides);
    // Fills the pages with these settings (popup() without showing).
    void load(const Preferences &preferences,
              const QList<Keymap::Command> &commands,
              const Keymap::Overrides &overrides);
    void applyTheme(const Theme &theme);
    // Names of the language servers found on this machine, for the Editor page.
    void setInstalledServers(const QStringList &names);

    Preferences preferences() const;

    Keymap::Overrides shortcutOverrides() const { return overrides_; }

    // Keymap page, public for tests: gives the selected command `shortcut`,
    // taking it from the commands that had it.
    void assignShortcut(const QKeySequence &shortcut);
    void selectCommand(const QString &id);

    QTreeWidget *commandList() const { return commandList_; }

    QLabel *keymapNote() const { return keymapNote_; }

signals:
    // OK or Apply: preferences() and shortcutOverrides() hold the changes.
    void applied();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    // The Full Line rows shown while it is on, the model for Ollama only.
    void updateFullLineRows();
    QWidget *createSidebar();
    // Miniatures of `themes`, four in a row, added to themes_.
    QWidget *themeGrid(const QList<Theme> &themes);
    QWidget *createAppearancePage();
    QWidget *createFontsPage();
    QWidget *createEditorPage();
    QWidget *createFilesPage();
    QWidget *createClaudePage();
    QWidget *createKeymapPage();
    // A page: its title and description above `content`, which scrolls.
    QWidget *page(const QString &title, const QString &description, QWidget *content, bool scrolls = true);
    void addPage(const QString &icon, const QString &title, QWidget *page);
    void updateFontPreview();

    // Keymap page.
    const Keymap::Command *selectedCommand() const;
    void fillCommandList();
    void updateCommandRows();
    void onCommandSelected();
    void setShortcuts(const Keymap::Command &command, const QList<QKeySequence> &shortcuts);
    // Below the shortcut: what the last change did; hidden when empty.
    void setNote(const QString &text);

    Theme theme_;
    QListWidget *pageList_ = nullptr;
    PopupHeader *header_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QStringList pageIcons_;

    QButtonGroup *themes_ = nullptr;
    QComboBox *fontFamily_ = nullptr;
    QLabel *fontSources_ = nullptr; // where to get the recommended fonts missing
    QSpinBox *fontSize_ = nullptr;
    QComboBox *fontWeight_ = nullptr;
    QDoubleSpinBox *lineHeight_ = nullptr;
    QCheckBox *ligatures_ = nullptr;
    QLabel *fontPreview_ = nullptr;
    QSpinBox *interfaceFontSize_ = nullptr;
    QSpinBox *treeFontSize_ = nullptr;
    QCheckBox *systemTitleBar_ = nullptr;
    QCheckBox *animations_ = nullptr;
    QCheckBox *codeFolding_ = nullptr;
    QCheckBox *wrapLines_ = nullptr;
    QCheckBox *showWhitespace_ = nullptr;
    QComboBox *autoCompletion_ = nullptr;
    QCheckBox *completionAcceptsEnter_ = nullptr;
    QCheckBox *languageServers_ = nullptr;
    QLabel *serversFound_ = nullptr;
    QCheckBox *fullLine_ = nullptr;
    QComboBox *fullLineServer_ = nullptr;
    QLineEdit *fullLineEndpoint_ = nullptr;
    QLineEdit *fullLineModel_ = nullptr;
    QWidget *fullLineServerRow_ = nullptr;
    QWidget *fullLineEndpointRow_ = nullptr;
    QWidget *fullLineModelRow_ = nullptr;
    QCheckBox *formatOnSave_ = nullptr;
    QCheckBox *alwaysSelectOpenedFile_ = nullptr;
    QCheckBox *snapshotBeforePrompt_ = nullptr;
    QCheckBox *shareEditorContext_ = nullptr;
    QCheckBox *reportEditErrors_ = nullptr;
    QCheckBox *exposeTools_ = nullptr;
    QCheckBox *gitThroughKDKTools_ = nullptr;
    QCheckBox *claudeNotifications_ = nullptr;
    TerminalPreferencesPage *terminalPage_ = nullptr;

    QList<Keymap::Command> commands_;
    Keymap::Overrides overrides_;
    QLineEdit *commandFilter_ = nullptr;
    QTreeWidget *commandList_ = nullptr;
    QLabel *selectedTitle_ = nullptr;
    QKeySequenceEdit *shortcutEdit_ = nullptr;
    QPushButton *removeShortcut_ = nullptr;
    QPushButton *resetShortcut_ = nullptr;
    QLabel *keymapNote_ = nullptr;
};
