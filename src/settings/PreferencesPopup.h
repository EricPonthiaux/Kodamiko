#pragma once

#include "Keymap.h"
#include "Preferences.h"
#include "ui/OverlayPopup.h"
#include "ui/Theme.h"

class QAbstractButton;
class QButtonGroup;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTreeWidget;
class ToggleSwitch;

// File ▸ Preferences, a popup over the window like Find in Files: a side
// bar of pages (Appearance, Editor, Files, Claude, Keymap), each made of
// cards of settings. Changes apply on OK or Apply; Cancel, Escape or a
// click elsewhere leave everything as it was.
class PreferencesPopup : public OverlayPopup {
    Q_OBJECT

public:
    explicit PreferencesPopup(const Theme &theme, QWidget *parent);

    // Shows the popup with these settings.
    void popup(const Preferences &preferences, const QList<Keymap::Command> &commands,
               const Keymap::Overrides &overrides);
    // Fills the pages with these settings (popup() without showing).
    void load(const Preferences &preferences, const QList<Keymap::Command> &commands,
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
    QWidget *createSidebar();
    QWidget *createAppearancePage();
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
    QStackedWidget *pages_ = nullptr;
    QStringList pageIcons_;

    QButtonGroup *themes_ = nullptr;
    QComboBox *fontFamily_ = nullptr;
    QSpinBox *fontSize_ = nullptr;
    QLabel *fontPreview_ = nullptr;
    ToggleSwitch *systemTitleBar_ = nullptr;
    ToggleSwitch *codeFolding_ = nullptr;
    ToggleSwitch *wrapLines_ = nullptr;
    ToggleSwitch *showWhitespace_ = nullptr;
    ToggleSwitch *autoCompletion_ = nullptr;
    ToggleSwitch *languageServers_ = nullptr;
    QLabel *serversFound_ = nullptr;
    ToggleSwitch *formatOnSave_ = nullptr;
    ToggleSwitch *alwaysSelectOpenedFile_ = nullptr;
    ToggleSwitch *snapshotBeforePrompt_ = nullptr;
    ToggleSwitch *claudeNotifications_ = nullptr;

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
