// SPDX-License-Identifier: GPL-3.0-only

// MainWindow: themes and preferences, applied to every part of the window.

#include "MainWindow.h"
#include "changes/BranchesPopup.h"
#include "changes/ChangesView.h"
#include "changes/NewBranchPopup.h"
#include "changes/SnapshotsPopup.h"
#include "changes/StashesPopup.h"
#include "claude/ClaudeConfigView.h"
#include "claude/EditorContext.h"
#include "claude/mcp/DuetTools.h"
#include "completion/CompletionController.h"
#include "completion/CompletionService.h"
#include "completion/FullLine.h"
#include "diagnostics/ProblemsView.h"
#include "editor/EditorTabs.h"
#include "editor/FindBar.h"
#include "history/HistoryView.h"
#include "lsp/LanguageServers.h"
#include "outline/OutlineView.h"
#include "search/FindInFilesPopup.h"
#include "settings/FontSettings.h"
#include "settings/Keymap.h"
#include "settings/Preferences.h"
#include "settings/PreferencesPopup.h"
#include "setup/PrerequisitesPopup.h"
#include "terminal/TerminalWidget.h"
#include "ui/ClaudeIndicator.h"
#include "ui/DetachablePage.h"
#include "ui/Motion.h"
#include "ui/NoticeBar.h"
#include "ui/ThemeStyle.h"
#include "ui/TitleBar.h"
#include "ui/UiIcons.h"
#include "ui/WindowFrame.h"
#include "welcome/NewProjectPopup.h"
#include "welcome/ProjectSettingsPopup.h"
#include "workspace/FileTreeView.h"
#include "workspace/FilesPanel.h"
#include "workspace/SidePanel.h"

#include <QActionGroup>
#include <QSettings>
#include <QToolButton>

QString MainWindow::savedThemeName()
{
    return Preferences::load().theme;
}

void MainWindow::setTheme(const QString &name)
{
    theme_ = Theme::named(name);
    Preferences::load().applyTypography(theme_);
    for (QAction *action : themeActions_->actions())
        action->setChecked(action->data().toString() == theme_.name);
    ThemeStyle::apply(theme_);
    titleBar_->applyTheme(theme_);
    claudeIndicator_->applyTheme(theme_);
    frame_->applyTheme(theme_);
    terminalWindow_->applyTheme(theme_);
    branchButton_->setIcon(UiIcons::icon(QStringLiteral("branch"), theme_.mutedText));
    fileTree_->setSearchHighlight(theme_.findHighlight);
    fileTree_->setDarkIcons(theme_.isDark);
    filesPanel_->setIconColor(theme_.foreground);
    claudeConfigView_->setDarkIcons(theme_.isDark);
    claudeConfigView_->setIconColor(theme_.foreground);
    outlineView_->setIconColor(theme_.foreground);
    problemsView_->setIconColor(theme_.foreground);
    problemsView_->setDarkIcons(theme_.isDark);
    sidebar_->applyTheme(theme_);
    editorTabs_->applyTheme(theme_);
    for (int i = 0; i < terminalTabs_->count(); ++i) {
        if (auto *terminal = qobject_cast<TerminalWidget *>(terminalTabs_->widget(i)))
            terminal->applyTheme(theme_);
    }
    changesView_->applyTheme(theme_);
    historyView_->applyTheme(theme_);
    findBar_->applyTheme(theme_);
    findInFiles_->applyTheme(theme_);
    serverNotice_->applyTheme(theme_);
    unsavedEditNotice_->applyTheme(theme_);
    toolRequestBar_->applyTheme(theme_);
    toolsNotice_->applyTheme(theme_);
    if (projectSettings_)
        projectSettings_->applyTheme(theme_);
    if (newProjectPopup_)
        newProjectPopup_->applyTheme(theme_);
    if (preferencesPopup_)
        preferencesPopup_->applyTheme(theme_);
    snapshotsPopup_->applyTheme(theme_);
    newBranchPopup_->applyTheme(theme_);
    branchesPopup_->applyTheme(theme_);
    stashesPopup_->applyTheme(theme_);
    if (prerequisitesPopup_)
        prerequisitesPopup_->applyTheme(theme_);
    if (modeTabs_) {
        updateTabIcons();
        updateClaudeIndicator();
        updateUsage();
    }
    QSettings().setValue(QStringLiteral("theme"), theme_.name);
}

void MainWindow::showPreferences()
{
    // Built on first use: it lists every font of the system.
    if (!preferencesPopup_) {
        preferencesPopup_ = new PreferencesPopup(theme_, this);
        preferencesPopup_->setInstalledServers(languageServers_->installed());
        connect(preferencesPopup_, &PreferencesPopup::applied, this, [this] {
            applyPreferences(preferencesPopup_->preferences());
            Keymap::saveOverrides(preferencesPopup_->shortcutOverrides());
            applyKeymap(preferencesPopup_->shortcutOverrides());
        });
    }
    preferencesPopup_->popup(Preferences::load(), Keymap::commands(titleBar_->menuBar()), Keymap::loadOverrides());
}

void MainWindow::applyFullLine(const Preferences &preferences)
{
    fullLineModel_->configure(preferences.fullLineServer == QLatin1String("llama.cpp")
                                  ? LocalFullLineModel::Server::LlamaCpp
                                  : LocalFullLineModel::Server::Ollama,
                              QUrl(preferences.fullLineEndpoint), preferences.fullLineModel);
    completion_->setFullLineModel(preferences.fullLineCompletion ? fullLineModel_ : nullptr);
}

void MainWindow::applyPreferences(const Preferences &preferences)
{
    const Preferences previous = Preferences::load();
    preferences.save();
    Motion::policy().setEnabled(preferences.animations);
    FontSettings::apply(preferences, previous, this);
    if (preferences.theme != previous.theme || preferences.codeFont() != previous.codeFont() ||
        preferences.codeLineHeight != previous.codeLineHeight || preferences.ligatures != previous.ligatures)
        setTheme(preferences.theme);
    // The menus' commands apply their own settings.
    systemTitleBar_->setChecked(preferences.systemTitleBar);
    formatOnSave_->setChecked(preferences.formatOnSave);
    snapshotBeforePrompt_->setChecked(preferences.snapshotBeforePrompt);
    shareEditorContext_->setChecked(preferences.shareEditorContext);
    exposeTools_->setChecked(preferences.exposeTools);
    claudeNotifications_->setChecked(preferences.claudeNotifications);
    editorTabs_->setEditorOptions(preferences.editor);
    languageServers_->setEnabled(preferences.languageServers);
    warmUpLanguageServers();
    applyFullLine(preferences);
    filesPanel_->setAlwaysSelectOpenedFile(preferences.alwaysSelectOpenedFile);
}
