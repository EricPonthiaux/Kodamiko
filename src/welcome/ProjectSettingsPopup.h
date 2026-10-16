#pragma once

#include "projects/ProjectConfig.h"
#include "projects/ProjectDetector.h"
#include "ui/OverlayPopup.h"
#include "ui/Theme.h"

class ProjectSetupPanel;
class Toolchains;

// File ▸ Project Settings: the setup of the open project's languages, and
// the download of what they need, over the main window.
class ProjectSettingsPopup : public OverlayPopup {
    Q_OBJECT

public:
    ProjectSettingsPopup(Toolchains *toolchains, const Theme &theme, QWidget *parent);

    // Shows `project`'s setup; `detected` when it was just detected.
    void popup(const ProjectConfig &project, const QList<DetectedLanguage> &detected = {});
    // Shows the downloads of what `project` lacks, at once.
    void download(const ProjectConfig &project);
    void applyTheme(const Theme &theme);

signals:
    // Saved, its tools downloaded (or skipped).
    void applied(const ProjectConfig &project);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    ProjectSetupPanel *panel_;
};
