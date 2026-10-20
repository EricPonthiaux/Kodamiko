// SPDX-License-Identifier: GPL-3.0-only

#include "ProjectSetupPanel.h"

#include "ProjectSetupForm.h"
#include "ProvisioningView.h"
#include "projects/ProjectStore.h"
#include "toolchains/InstallPlan.h"
#include "toolchains/Toolchains.h"
#include "ui/PopupWidgets.h"

#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

QString headerIcon(ProjectSetupPanel::Purpose purpose)
{
    switch (purpose) {
    case ProjectSetupPanel::Purpose::FirstSetup:
        return QStringLiteral("files");
    case ProjectSetupPanel::Purpose::Settings:
        return QStringLiteral("settings");
    }
    return {};
}

} // namespace

ProjectSetupPanel::ProjectSetupPanel(Toolchains *toolchains, Purpose purpose, QWidget *parent)
    : QWidget(parent)
    , toolchains_(toolchains)
    , purpose_(purpose)
    , header_(new PopupHeader(headerIcon(purpose), QString(), this))
    , pages_(new QStackedWidget(this))
    , form_(new ProjectSetupForm(toolchains, this))
    , provisioning_(new ProvisioningView(toolchains, this))
{
    connect(header_, &PopupHeader::closeRequested, this, [this] {
        if (provisioning_->isRunning())
            provisioning_->cancel();
        emit cancelled();
    });

    // The pages: the languages, then the downloads.
    auto *downloads = new QWidget;
    downloads->setObjectName(QStringLiteral("pageContent"));
    auto *downloadsLayout = new QVBoxLayout(downloads);
    downloadsLayout->setContentsMargins(16, 12, 16, 16);
    downloadsLayout->addWidget(provisioning_);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("pageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(downloads);
    pages_->addWidget(form_);
    pages_->addWidget(scroll);
    connect(form_, &ProjectSetupForm::changed, this, &ProjectSetupPanel::updateSummary);
    connect(provisioning_, &ProvisioningView::finished, this, &ProjectSetupPanel::onDownloadsFinished);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addWidget(pages_, 1);
    layout->addWidget(createFooter());
}

QWidget *ProjectSetupPanel::createFooter()
{
    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("popupFooter"));
    summary_ = new QLabel(footer);
    summary_->setObjectName(QStringLiteral("footerHint"));
    summary_->setWordWrap(true);
    const bool isFirstSetup = purpose_ == Purpose::FirstSetup;
    cancel_ = new QPushButton(isFirstSetup ? tr("Back") : tr("Cancel"), footer);
    retry_ = new QPushButton(tr("Retry"), footer);
    skip_ = new QPushButton(tr("Continue Without Them"), footer);
    apply_ = new QPushButton(tr("Apply"), footer);
    ok_ = new QPushButton(isFirstSetup ? tr("Open Project") : tr("OK"), footer);
    ok_->setDefault(true);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (provisioning_->isRunning())
            provisioning_->cancel();
        if (pages_->currentIndex() == 1) {
            showForm();
            return;
        }
        emit cancelled();
    });
    connect(retry_, &QPushButton::clicked, this, [this] { startDownloads(provisioning_->failedSteps()); });
    connect(skip_, &QPushButton::clicked, this, &ProjectSetupPanel::finish);
    connect(apply_, &QPushButton::clicked, this, [this] { save(Then::StayOpen); });
    connect(ok_, &QPushButton::clicked, this, [this] { save(Then::Close); });

    auto *layout = new QHBoxLayout(footer);
    layout->setContentsMargins(16, 10, 16, 10);
    layout->setSpacing(8);
    layout->addWidget(summary_, 1);
    for (QPushButton *button : {cancel_, retry_, skip_, apply_, ok_}) {
        button->setMinimumWidth(84);
        layout->addWidget(button);
    }
    return footer;
}

void ProjectSetupPanel::applyTheme(const Theme &theme)
{
    header_->applyTheme(theme);
    form_->applyTheme(theme);
}

void ProjectSetupPanel::load(const ProjectConfig &project, const QList<DetectedLanguage> &detected)
{
    project_ = project;
    form_->setProject(project, detected);
    showForm();
}

void ProjectSetupPanel::updateTitle()
{
    switch (purpose_) {
    case Purpose::FirstSetup:
        header_->setTitle(tr("Set Up %1").arg(project_.name));
        header_->setSubtitle(QDir::toNativeSeparators(project_.path));
        return;
    case Purpose::Settings:
        header_->setTitle(tr("Project Settings"));
        header_->setSubtitle(
            tr("%1 · %2").arg(project_.name, QDir::toNativeSeparators(QDir(project_.path).absolutePath())));
        return;
    }
}

void ProjectSetupPanel::showForm()
{
    pages_->setCurrentIndex(0);
    updateTitle();
    cancel_->setText(purpose_ == Purpose::FirstSetup ? tr("Back") : tr("Cancel"));
    retry_->hide();
    skip_->hide();
    apply_->setVisible(purpose_ == Purpose::Settings);
    ok_->show();
    updateSummary();
}

void ProjectSetupPanel::updateSummary()
{
    if (pages_->currentIndex() != 0)
        return;
    const ProjectConfig project = form_->project();
    const QList<InstallStep> steps = InstallPlan::plan(project, *toolchains_);
    QStringList lines;
    if (!steps.isEmpty())
        lines.append(tr("Will download: %1").arg(InstallPlan::summary(steps)));
    lines.append(InstallPlan::unavailable(project, *toolchains_));
    summary_->setText(lines.isEmpty() ? tr("All tools available") : lines.join(QLatin1Char('\n')));
}

void ProjectSetupPanel::save(Then then)
{
    if (pages_->currentIndex() != 0)
        return;
    then_ = then;
    saveAndDownload(form_->project());
}

void ProjectSetupPanel::download(const ProjectConfig &project)
{
    then_ = Then::Close;
    saveAndDownload(project);
}

void ProjectSetupPanel::saveAndDownload(const ProjectConfig &project)
{
    project_ = project;
    // The setup is kept even if the downloads fail.
    ProjectStore::save(project_);
    const QList<InstallStep> steps = InstallPlan::plan(project_, *toolchains_);
    if (steps.isEmpty()) {
        finish();
        return;
    }
    header_->setTitle(tr("Downloading the Tools of %1").arg(project_.name));
    header_->setSubtitle(QDir::toNativeSeparators(project_.path));
    startDownloads(steps);
}

void ProjectSetupPanel::finish()
{
    if (then_ == Then::Close) {
        emit done(project_);
        return;
    }
    // Applied: the form again, the tools now downloaded.
    emit applied(project_);
    load(project_);
}

void ProjectSetupPanel::startDownloads(const QList<InstallStep> &steps)
{
    pages_->setCurrentIndex(1);
    summary_->setText(tr("Downloading %1…").arg(InstallPlan::summary(steps)));
    cancel_->setText(tr("Cancel"));
    retry_->hide();
    skip_->hide();
    apply_->hide();
    ok_->hide();
    provisioning_->start(steps);
}

void ProjectSetupPanel::onDownloadsFinished(bool ok)
{
    if (ok) {
        finish();
        return;
    }
    if (pages_->currentIndex() != 1 || provisioning_->failedSteps().isEmpty())
        return; // cancelled
    summary_->setText(tr("Some tools could not be downloaded; their languages "
                         "complete with the project's words."));
    cancel_->setText(tr("Back"));
    retry_->show();
    skip_->show();
}

bool ProjectSetupPanel::isDownloading() const
{
    return provisioning_->isRunning();
}
