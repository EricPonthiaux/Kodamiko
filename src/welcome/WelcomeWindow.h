// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projects/ProjectConfig.h"
#include "ui/Theme.h"

#include <QWidget>

#include <memory>

class AboutPopup;
class NewProjectForm;
class ProjectSetupPanel;
class QBoxLayout;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class Toolchains;
class WindowButton;
class WindowFrame;

// The window Duet opens with, before the main one, in the manner of
// IntelliJ's welcome screen: the projects opened before, to search and
// reopen (those whose folder is gone listed apart, to locate), and on the
// side New Project to create one (NewProjectForm) and Import Project to
// add a folder. A project not set up yet
// gets its languages chosen first; then what it needs is downloaded, and
// projectChosen comes. Without any project, it shows how to start one.
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

    // The projects, the headings of their groups among them when some
    // folders are gone (ProjectListDelegate::headingRole).
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
    // Duet's name and version, New Project and Import Project, About.
    QWidget *createSide();
    // The search, the list, the shortcuts below, or the empty state.
    QWidget *createProjectsPage();
    QWidget *createEmptyState();
    QWidget *createKeyHints();
    // New Project and Import Project, for the side and the empty state.
    void addProjectButtons(QBoxLayout *layout, QWidget *parent);
    void addHeading(const QString &title);
    QListWidgetItem *addProject(const ProjectConfig &project);
    void importProject();
    void showProjects();
    void showAbout();
    void filter(const QString &text);
    void showContextMenu(const QPoint &position);
    void showMenu(QListWidgetItem *item, QPoint globalPosition);
    // Asks where the folder of the project at `path` went, and moves it there.
    void locate(const QString &path);
    void remove(QListWidgetItem *item);

    Theme theme_;
    std::unique_ptr<Toolchains> toolchains_;
    WindowFrame *frame_;
    QStackedWidget *pages_;
    QLineEdit *search_;
    QListWidget *list_;
    ProjectSetupPanel *setup_;
    NewProjectForm *newProject_;
    WindowButton *close_ = nullptr;
    QWidget *keyHints_ = nullptr;
    QWidget *emptyState_ = nullptr;
    AboutPopup *about_ = nullptr; // built on first use
};
