#pragma once

#include "projects/ProjectConfig.h"
#include "projects/ProjectDetector.h"
#include "syntax/Theme.h"
#include "toolchains/InstallPlan.h"

#include <QWidget>

class ProjectSetupForm;
class ProvisioningView;
class QLabel;
class QPushButton;
class QStackedWidget;
class Toolchains;

// Setting up a project, for the welcome window and Project Settings: its
// languages (ProjectSetupForm), then the download of what they need
// (ProvisioningView). `done` comes once the setup is saved and its tools
// are there, or the user chose to go on without them.
class ProjectSetupPanel : public QWidget {
    Q_OBJECT

public:
    // `actionText` names the button that saves ("Open Project", "Apply").
    ProjectSetupPanel(Toolchains *toolchains, const QString &actionText, QWidget *parent = nullptr);

    // Shows `project`'s setup; `detected`, why each language was found.
    void load(const ProjectConfig &project, const QList<DetectedLanguage> &detected = {});
    // Saves the setup, then downloads what is missing.
    void apply();
    // Saves the setup and downloads what is missing, without the form
    // (the tools a project opened from the command line lacks).
    void download(const ProjectConfig &project);
    bool isDownloading() const;
    void applyTheme(const Theme &theme);

    ProjectSetupForm *form() const { return form_; }

signals:
    void done(const ProjectConfig &project);
    // Back to the list, or the popup closed.
    void cancelled();

private:
    void showForm();
    void startDownloads(const QList<InstallStep> &steps);
    void updateSummary();
    void onDownloadsFinished(bool ok);

    Toolchains *toolchains_;
    ProjectConfig project_;
    QLabel *headerIcon_;
    QLabel *title_;
    QLabel *location_;
    QStackedWidget *pages_;
    ProjectSetupForm *form_;
    ProvisioningView *provisioning_;
    QLabel *summary_;
    QPushButton *back_;
    QPushButton *detect_;
    QPushButton *retry_;
    QPushButton *skip_;
    QPushButton *action_;
    Theme theme_;
};
