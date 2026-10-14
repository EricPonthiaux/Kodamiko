#pragma once

#include "claude/HookServer.h"
#include "syntax/Theme.h"

#include <QMainWindow>

class DiffView;
class EditorTabs;
class FileTreeView;
class OutlineView;
class QAction;
class QActionGroup;
class QSplitter;
class QStackedWidget;
class QTabBar;
class QTabWidget;
class TerminalWidget;
struct ProcessSpec;

// Fenêtre principale à trois modes plein écran, choisis par les onglets en
// bas à droite : Terminal, Edit (panneau Fichiers / Structure | onglets
// d'éditeur) et Diff (changements en cours). Assemble les modules et relaie leurs
// signaux.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    enum class Mode { Terminal, Edit, Diff };

    explicit MainWindow(const QString &folder, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createWidgets();
    QWidget *createEditPage();
    QWidget *createTerminalPage();
    void createModeTabs();
    void createMenus();
    void restoreSettings();

    void openFolder(const QString &folder);
    void chooseFolder();
    void chooseFile();
    void newClaudeSession();
    void newShell();
    void addTerminal(const QString &title, const ProcessSpec &spec);
    void setDarkTheme(bool dark);
    void setMode(Mode mode);
    void toggleTerminalMode();
    void showOutline();
    void updateTerminalPage();

    void onHookReceived(const HookEvent &event);

    QString absolutePath(const QString &relativePath) const;

    Theme theme_;
    QString folder_;
    HookServer hookServer_;

    EditorTabs *editorTabs_ = nullptr;
    FileTreeView *fileTree_ = nullptr;
    OutlineView *outlineView_ = nullptr;
    QTabWidget *sidebar_ = nullptr;
    QTabWidget *terminalTabs_ = nullptr;
    QStackedWidget *terminalPage_ = nullptr;
    DiffView *diffView_ = nullptr;
    QSplitter *editSplitter_ = nullptr;

    QStackedWidget *modes_ = nullptr;
    QTabBar *modeTabs_ = nullptr;
    QActionGroup *modeActions_ = nullptr;
    QAction *showSidebar_ = nullptr;
    Mode mode_ = Mode::Edit;
    Mode modeBeforeTerminal_ = Mode::Edit;
};
