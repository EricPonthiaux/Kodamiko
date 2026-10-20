#pragma once

#include "projects/ProjectConfig.h"
#include "ui/Theme.h"

#include <QWidget>

#include <memory>

class NewProjectForm;
class ProjectSetupPanel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class Toolchains;
class WindowFrame;

// The window Duet opens with, before the main one, in the manner of
// IntelliJ's welcome screen: the projects opened before, to search and
// reopen, New Project to create one (NewProjectForm) and Import Project to
// add a folder. A project not set up yet
// gets its languages chosen first; then what it needs is downloaded, and
// projectChosen comes.
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
    void importProject();
    void showProjects();
    void filter(const QString &text);
    // The languages `project` uses, as shown under its name; none until set up.
    static QStringList shownLanguages(const ProjectConfig &project);
    void showContextMenu(const QPoint &position);
    void remove(QListWidgetItem *item);

    Theme theme_;
    std::unique_ptr<Toolchains> toolchains_;
    WindowFrame *frame_;
    QStackedWidget *pages_;
    QLineEdit *search_;
    QListWidget *list_;
    ProjectSetupPanel *setup_;
    NewProjectForm *newProject_;
};
