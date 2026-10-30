// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projects/ProjectConfig.h"
#include "ui/Theme.h"

#include <QWidget>

#include <memory>

class AboutPopup;
class NewProjectForm;
class ProjectSetupPanel;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;
class Toolchains;
class WindowButton;
class WindowFrame;

// The startup workspace: a full-width list of recent projects, with creation
// and opening actions below. Missing folders are grouped separately
// so they can be located. A project not set up yet gets its languages chosen
// first, then its tools downloaded, before projectChosen is emitted.
class WelcomeWindow : public QWidget {
    Q_OBJECT

public:
    explicit WelcomeWindow(QWidget *parent = nullptr);
    ~WelcomeWindow() override;

    // Lists the projects again (after one was removed or opened).
    void reload();
    // Opens `path`: sets it up first if needed, then downloads its tools.
    void open(const QString &path);
    // Shows the setup of `path`, detected again if it was never set up.
    void configure(const QString &path);

    // Every project, most recently opened first, with group headings when
    // some folders are gone (ProjectListDelegate::headingRole).
    QListWidget *projectList() const { return list_; }

    // The New Project page, shown by its button. For tests.
    NewProjectForm *newProjectForm() const { return newProject_; }

    void showNewProject();

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void projectChosen(const ProjectConfig &project);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    // Built in WelcomeWindowUi.cpp.
    // The window's logo, name and close button.
    QWidget *createTitleBar();
    // The projects, creation/opening actions and footer, stacked vertically.
    QWidget *createWelcomePage();
    QWidget *createProjectsPanel();
    // "Recent projects", their count and the search.
    QWidget *createListHeading();
    // What the panel shows without any project.
    QWidget *createFirstSteps();
    QWidget *createProjectButtons();
    // About, the version and the keyboard's shortcuts.
    QWidget *createFooter();
    void addShortcuts(QWidget *page);

    // The delegate and the menus of the projects' list.
    void configureList();
    void addHeading(const QString &title);
    QListWidgetItem *addProject(const ProjectConfig &project);
    // The texts and parts of the panel for `projectCount` projects: none on
    // a first launch.
    void showProjectsState(qsizetype projectCount);
    void importProject();
    void showProjects();
    // `page` in place of the welcome and its title bar, the window grown to
    // fit it.
    void showForm(QWidget *page);
    void showAbout();
    void filter(const QString &text);
    // The first matching project, the search for no matches, or New Project.
    void focusFirstChoice();
    QListWidgetItem *firstShownProject() const;
    void showContextMenu(const QPoint &position);
    void showMenu(QListWidgetItem *item, QPoint globalPosition);
    // Asks where the folder of the project at `path` went, and moves it there.
    void locate(const QString &path);
    void remove(QListWidgetItem *item);
    bool moveWindow(QObject *watched, const QEvent *event);
    bool searchKeyPressed(const QKeyEvent *key);
    bool listKeyPressed(const QKeyEvent *key);

    Theme theme_;
    std::unique_ptr<Toolchains> toolchains_;
    WindowFrame *frame_;
    QStackedWidget *pages_;
    QLineEdit *search_;
    QListWidget *list_;
    ProjectSetupPanel *setup_;
    NewProjectForm *newProject_;
    QWidget *titleBar_ = nullptr; // over the welcome, not the forms
    QWidget *welcomePage_ = nullptr;
    WindowButton *close_ = nullptr;
    QLabel *count_ = nullptr;
    QLabel *noMatch_ = nullptr;
    QWidget *firstSteps_ = nullptr;
    QPushButton *newButton_ = nullptr;
    QWidget *navigationHints_ = nullptr;
    AboutPopup *about_ = nullptr; // built on first use
};
