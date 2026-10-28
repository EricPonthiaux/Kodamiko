// SPDX-License-Identifier: GPL-3.0-only

#include "ProjectSettingsPopup.h"

#include "ProjectSetupPanel.h"
#include "ui/PopupWidgets.h"

#include <QKeyEvent>
#include <QVBoxLayout>

ProjectSettingsPopup::ProjectSettingsPopup(Toolchains *toolchains, const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("projectSettings"), QSizeF(0.7, 0.86), QSize(960, 680), parent)
    , panel_(new ProjectSetupPanel(toolchains, ProjectSetupPanel::Purpose::Settings, this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->addWidget(panel_);
    connect(panel_, &ProjectSetupPanel::cancelled, this, &OverlayPopup::dismiss);
    connect(panel_, &ProjectSetupPanel::applied, this, &ProjectSettingsPopup::applied);
    connect(panel_, &ProjectSetupPanel::done, this, [this](const ProjectConfig &project) {
        dismiss();
        emit applied(project);
    });
    applyTheme(theme);
}

void ProjectSettingsPopup::applyTheme(const Theme &theme)
{
    setStyleSheet(popupStyleSheet(theme));
    panel_->applyTheme(theme);
}

void ProjectSettingsPopup::popup(const ProjectConfig &project, const QList<DetectedLanguage> &detected)
{
    if (!panel_->isDownloading())
        panel_->load(project, detected);
    showCentered();
}

void ProjectSettingsPopup::download(const ProjectConfig &project)
{
    showCentered();
    // Nothing missing: done at once, and hidden again.
    if (!panel_->isDownloading()) {
        panel_->load(project);
        panel_->download(project);
    }
}

void ProjectSettingsPopup::keyPressEvent(QKeyEvent *event)
{
    // Escape hides it; downloads go on behind.
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}
