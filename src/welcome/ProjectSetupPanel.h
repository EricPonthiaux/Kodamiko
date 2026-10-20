// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projects/ProjectConfig.h"
#include "projects/ProjectDetector.h"
#include "toolchains/InstallPlan.h"
#include "ui/Theme.h"

#include <QWidget>

class PopupHeader;
class ProjectSetupForm;
class ProvisioningView;
class QLabel;
class QPushButton;
class QStackedWidget;
class Toolchains;

// Setting up a project, for the welcome window and Project Settings: its
// languages (ProjectSetupForm), then the download of what they need
// (ProvisioningView). The setup is saved first, the downloads follow; the
// footer says beforehand what will be downloaded.
class ProjectSetupPanel : public QWidget {
    Q_OBJECT

public:
    enum class Purpose {
        // A new project, from the welcome window: Back, Open Project.
        FirstSetup,
        // Project Settings: Cancel, Apply (saves, stays open), OK.
        Settings,
    };

    ProjectSetupPanel(Toolchains *toolchains, Purpose purpose, QWidget *parent = nullptr);

    // Shows `project`'s setup; `detected`, why each language was found.
    void load(const ProjectConfig &project, const QList<DetectedLanguage> &detected = {});
    // Saves the setup and downloads what is missing, without the form
    // (the tools a project opened from the command line lacks).
    void download(const ProjectConfig &project);
    bool isDownloading() const;
    void applyTheme(const Theme &theme);

    ProjectSetupForm *form() const { return form_; }

    // The footer's buttons. For tests.
    QPushButton *applyButton() const { return apply_; }

    QPushButton *okButton() const { return ok_; }

    QPushButton *cancelButton() const { return cancel_; }

signals:
    // Saved by Apply, its tools downloaded (or skipped); the form stays.
    void applied(const ProjectConfig &project);
    // Saved by OK or Open Project, its tools downloaded (or skipped).
    void done(const ProjectConfig &project);
    // Back to the list, or the popup closed.
    void cancelled();

private:
    // What follows the downloads: the form again, or the end.
    enum class Then {
        StayOpen,
        Close
    };

    QWidget *createFooter();
    void save(Then then);
    void saveAndDownload(const ProjectConfig &project);
    void finish();
    void showForm();
    void startDownloads(const QList<InstallStep> &steps);
    void updateSummary();
    void onDownloadsFinished(bool ok);
    void updateTitle();

    Toolchains *toolchains_;
    Purpose purpose_;
    Then then_ = Then::Close;
    ProjectConfig project_;
    PopupHeader *header_;
    QStackedWidget *pages_;
    ProjectSetupForm *form_;
    ProvisioningView *provisioning_;
    QLabel *summary_ = nullptr;
    QPushButton *cancel_ = nullptr;
    QPushButton *retry_ = nullptr;
    QPushButton *skip_ = nullptr;
    QPushButton *apply_ = nullptr;
    QPushButton *ok_ = nullptr;
};
