#pragma once

#include "claude/ClaudeActivity.h"
#include "claude/HookServer.h"
#include "claude/mcp/McpRegistry.h"
#include "navigation/RecentFiles.h"
#include "projects/ProjectConfig.h"
#include "settings/Keymap.h"
#include "ui/Theme.h"

#include <QJsonObject>
#include <QMainWindow>
#include <QSet>

#include <memory>
#include <optional>

class AskpassServer;
class BranchesPopup;
class ChangesView;
class ClaudeConfigView;
class CodeIntelligence;
class CodeNavigator;
class CommandPalette;
class CompletionService;
class EditorStatus;
class EditorTabs;
class EditorWidget;
class FileStatusReader;
class FileTreeView;
class FilesPanel;
class FindBar;
class FindInFilesPopup;
class GitActions;
class GitCommands;
class GoToPopup;
class HistoryView;
class LanguageServers;
class LocalFullLineModel;
class NewBranchPopup;
class NoticeBar;
class OutlineView;
class PreferencesPopup;
class PrerequisitesDialog;
class ProblemStore;
class ProblemsView;
class ProjectSettingsPopup;
class QAction;
class QActionGroup;
class QLabel;
class QSplitter;
class QStackedWidget;
class QSystemTrayIcon;
class QTabBar;
class QTabWidget;
class QToolButton;
class RepositoryWatcher;
class Runner;
class SidePanel;
class SnapshotsPopup;
class StashesPopup;
class SymbolIndex;
class TerminalWidget;
class TitleBar;
class Toolchains;
class WindowFrame;
struct Preferences;
struct ProcessSpec;
struct RunConfiguration;

// Main window with four full-screen modes, chosen with the tabs at the
// bottom right: Terminal, Edit (Files / Outline / Claude panel | editor
// tabs), Diff (pending changes, commits) and History. Wires up the modules and relays their
// signals.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    enum class Mode {
        Terminal,
        Edit,
        Diff,
        History
    };

    explicit MainWindow(const QString &folder, QWidget *parent = nullptr);
    ~MainWindow() override;

signals:
    // File ▸ Close Project: the window closes, the welcome window should
    // show. Emitted before it closes.
    void projectClosed();

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void createWidgets();
    void createGitServices();
    void createPopups();
    QWidget *createEditPage();
    QWidget *createTerminalPage();
    void createModeTabs();
    void createStatusBar();
    void createMenus();
    void createGitMenu();
    void createEditMenu();
    void createFoldingMenu(QMenu *edit);
    // The current editor in Edit mode, null otherwise.
    EditorWidget *editorInEditMode() const;
    void createNavigateMenu();
    void createRunMenu();
    void createHelpMenu();
    void restoreSettings();

    void openFolder(const QString &folder);
    // Open files and expanded tree, remembered per working folder.
    void saveSession();
    void restoreSession();
    void chooseFolder();
    void chooseFile();
    // File ▸ Project Settings.
    void showProjectSettings();
    void applyProject(const ProjectConfig &project);
    void closeProject();
    // Says when the project lacks tools Duet can download.
    void updateToolsNotice();
    bool startClaudeSession(QString *error = nullptr, const QStringList &extraArguments = {});
    void restartClaudeSession();
    // The Effective Settings tab, opened, or updated if it is open.
    void showEffectiveSettings(bool open);
    void showClaudeSession();
    void newShell();
    TerminalWidget *addTerminal(const QString &title, const ProcessSpec &spec);
    // Runs `configuration` in its terminal tab (the one of its last run,
    // replaced), after saving the files. A build runs out of sight, the
    // Problems pane shows what it found; the others show their terminal.
    void runConfiguration(const RunConfiguration &configuration);
    void runSelected();
    void buildProject();
    // Stops the run in front of the user, else the chosen configuration's,
    // else the last started.
    void stopRun();
    void onRunFinished(const RunConfiguration &configuration, int exitCode, int errors, int warnings);
    // Opens .duet/run.json, created from the chosen configuration.
    void editRunConfigurations();
    // The configurations in `menu`: choosing one selects it, and runs it
    // when `run`.
    void fillRunMenu(QMenu *menu, bool run);
    // The title bar's run chip and the Run and Stop commands.
    void updateRunWidgets();
    // The icon of a run's terminal tab, of its kind, colored by its outcome.
    void setRunTabIcon(TerminalWidget *terminal, const QString &iconName, const QColor &color);
    // F2 / Shift+F2: the next or previous problem of the current file.
    void goToProblem(bool forward);
    // "duet", "dark", "purple" or "light" (see Theme::all), applied everywhere.
    void setTheme(const QString &name);
    static QString savedThemeName();
    void setMode(Mode mode);
    void toggleTerminalMode();
    void showOutline();
    void updateTerminalPage();

    void onHookReceived(const HookEvent &event);
    // Dot on the Claude tab and on the Terminal mode tab.
    void updateClaudeIndicator();
    void updateTabIcons();
    // Duet's frame and title bar, or the system's (View ▸ Use System
    // Title Bar). On macOS, the system's traffic lights over Duet's bar.
    void setSystemTitleBar(bool system);
    // Projects menu of the title bar, recent folders first.
    void updateProjectMenu();
    // Tells ClaudeActivity whether the user is looking at the session.
    void updateClaudeWatched();
    // Taskbar alert and, when the window is inactive, desktop notification.
    void notify(const QString &title, const QString &message);
    // Snapshot of the pending changes, so that the state before a prompt
    // can be restored.
    void snapshotBeforePrompt(const QString &prompt);
    void shareEditorContext();
    // The tools, resources and prompts of the editor's MCP server.
    void registerMcpTools();
    // Asks about the next sensitive tool Claude wants to run.
    void showToolRequest();
    void guardUnsavedEdits(const QStringList &paths);
    void followDiagramEdits(const QStringList &paths);
    // Asks what Claude should draw in `folder`, then shows the drawing.
    void generateDiagram(const QString &folder);
    void drawDiagram(const QString &path, const QString &kindTitle, const QString &description);
    // Context and plan usage, from the status line data.
    void updateUsage();
    void showCommandPalette();
    // The current tab changed: find bar, status bar widgets, recent files.
    void onCurrentTabChanged();
    // Bar above the editor when the current file's language server cannot
    // run, saying why.
    void updateServerNotice();
    // Starts the language server of the editor's file ahead of the first
    // completion (see LanguageServers::warmUp); of every open editor.
    void warmUpLanguageServer(EditorWidget *editor);
    void warmUpLanguageServers();
    // Find bar of the editor, in Edit mode only.
    void showFind(bool withReplace);
    void showGoTo(int tab);
    void showFindInFiles();
    // Next or previous change mark, or the one at the cursor rolled back.
    void goToChange(bool forward);
    void rollbackChange();
    // Alt+F1: the current file in the Files tree.
    void selectOpenedFile();
    // File tree commands.
    void sendToClaude(const QString &path);
    void runGitOnTree(const QString &title, const QString &path, bool rollback);
    void updateClaudeMarks();
    // Rereads the current branch and watches the project's git folder.
    void updateRepositoryInfo();
    // After a repository change: branch and, if they are displayed, Diff
    // and History modes.
    void onRepositoryChanged();
    void showSnapshots();
    // Git ▸ New Branch: the popup, then the branch created at HEAD.
    void showNewBranch();
    void createBranch(const QString &name, bool switchTo);
    // Git ▸ Branches and Git ▸ Stashes.
    void showBranches();
    void showStashes();
    // What git or ssh asks while talking to a remote: a password, a key
    // passphrase, whether to trust a host. Nothing when the user cancels.
    std::optional<QString> askCredentials(const QString &prompt);
    // Help ▸ Check Prerequisites, also shown at startup if git or claude is missing.
    void showPrerequisites();
    // File ▸ Preferences.
    void showPreferences();
    void applyPreferences(const Preferences &preferences);
    // Full Line completion on or off, with its server.
    void applyFullLine(const Preferences &preferences);
    // The side trees' text, rows and indentation in `font`.
    void applyTreeFont(const QFont &font);
    // Menu shortcuts, with the user's changes; QScintilla leaves them to the
    // menus.
    void applyKeymap(const Keymap::Overrides &overrides);

    QString absolutePath(const QString &relativePath) const;

    Theme theme_;
    QString folder_;
    HookServer hookServer_;
    // What the editor's MCP server offers Claude (see DuetTools).
    McpRegistry mcpRegistry_;
    ClaudeActivity claudeActivity_;
    QJsonObject claudeStatus_; // last status line data

    EditorTabs *editorTabs_ = nullptr;
    FilesPanel *filesPanel_ = nullptr;
    FileTreeView *fileTree_ = nullptr;
    FileStatusReader *fileStatuses_ = nullptr;
    bool inRepository_ = false;
    // Files written by Claude since the last prompt: a dot in the tree.
    QSet<QString> claudeTouched_;
    SymbolIndex *symbolIndex_ = nullptr;
    CompletionService *completion_ = nullptr;
    LocalFullLineModel *fullLineModel_ = nullptr;
    LanguageServers *languageServers_ = nullptr;
    std::unique_ptr<Toolchains> toolchains_;
    ProjectConfig project_;
    ProjectSettingsPopup *projectSettings_ = nullptr;
    NoticeBar *toolsNotice_ = nullptr;
    bool closingProject_ = false;
    NoticeBar *serverNotice_ = nullptr;
    QSet<QString> dismissedServerNotices_;   // closed by the user, by message
    NoticeBar *unsavedEditNotice_ = nullptr; // an edit by Claude refused
    // Claude asks to run a sensitive tool (restore a snapshot, commit…):
    // above every mode, one request at a time.
    NoticeBar *toolRequestBar_ = nullptr;
    struct ToolRequest {
        QString text;
        std::function<void(bool)> answer;
    };
    QList<ToolRequest> toolRequests_;
    CodeNavigator *navigator_ = nullptr;
    CodeIntelligence *intelligence_ = nullptr;
    OutlineView *outlineView_ = nullptr;
    ProblemStore *problemStore_ = nullptr;
    ProblemsView *problemsView_ = nullptr;
    Runner *runner_ = nullptr;
    QMenu *runChooser_ = nullptr; // the title bar chip's menu
    QAction *runAction_ = nullptr;
    QAction *buildAction_ = nullptr;
    QAction *stopAction_ = nullptr;
    ClaudeConfigView *claudeConfigView_ = nullptr;
    SidePanel *sidebar_ = nullptr;
    QTabWidget *terminalTabs_ = nullptr;
    QToolButton *newTerminalButton_ = nullptr;      // "+" at the right of the terminal tabs
    QToolButton *emptyNewTerminalButton_ = nullptr; // the same, when no terminal is open
    TerminalWidget *claudeTerminal_ = nullptr;
    QStackedWidget *terminalPage_ = nullptr;
    ChangesView *changesView_ = nullptr;
    HistoryView *historyView_ = nullptr;
    GitCommands *gitCommands_ = nullptr;
    GitActions *gitActions_ = nullptr;
    AskpassServer *askpass_ = nullptr;
    RepositoryWatcher *repositoryWatcher_ = nullptr;
    TitleBar *titleBar_ = nullptr;
    QToolButton *branchButton_ = nullptr;    // status bar: branch, Branches popup
    QToolButton *syncButton_ = nullptr;      // status bar: fetch, pull, push
    QToolButton *cancelGitButton_ = nullptr; // status bar: stops a fetch, push…
    QString gitProgress_;                    // the status bar message of a fetch, push… in progress
    QAction *continueAction_ = nullptr;      // Git menu: the operation in progress
    QAction *skipAction_ = nullptr;
    QAction *abortAction_ = nullptr;
    WindowFrame *frame_ = nullptr;
    QMenu *projectMenu_ = nullptr;
    QAction *goToFileAction_ = nullptr;
    QAction *findInFilesAction_ = nullptr;
    QAction *paletteAction_ = nullptr;
    QAction *systemTitleBar_ = nullptr;
    QAction *formatOnSave_ = nullptr;
    QActionGroup *themeActions_ = nullptr;
    QLabel *usageLabel_ = nullptr;
    CommandPalette *commandPalette_ = nullptr;
    FindBar *findBar_ = nullptr;
    EditorStatus *editorStatus_ = nullptr;
    GoToPopup *goToPopup_ = nullptr;
    FindInFilesPopup *findInFiles_ = nullptr;
    PreferencesPopup *preferencesPopup_ = nullptr;
    SnapshotsPopup *snapshotsPopup_ = nullptr;
    NewBranchPopup *newBranchPopup_ = nullptr;
    BranchesPopup *branchesPopup_ = nullptr;
    StashesPopup *stashesPopup_ = nullptr;
    RecentFiles recentFiles_;
    PrerequisitesDialog *prerequisitesDialog_ = nullptr;
    QSystemTrayIcon *trayIcon_ = nullptr;
    QAction *snapshotBeforePrompt_ = nullptr;
    QAction *shareEditorContext_ = nullptr;
    QAction *exposeTools_ = nullptr;
    QAction *claudeNotifications_ = nullptr;
    QSplitter *editSplitter_ = nullptr;

    QStackedWidget *modes_ = nullptr;
    QTabBar *modeTabs_ = nullptr;
    QActionGroup *modeActions_ = nullptr;
    QAction *showSidebar_ = nullptr;
    Mode mode_ = Mode::Edit;
    Mode modeBeforeTerminal_ = Mode::Edit;
};
