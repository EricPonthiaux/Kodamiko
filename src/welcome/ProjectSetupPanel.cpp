// SPDX-License-Identifier: GPL-3.0-only

#include "ProjectSetupPanel.h"

#include "ProjectSetupForm.h"
#include "ProvisioningView.h"
#include "projects/ProjectStore.h"
#include "recipes/RecipeLibrary.h"
#include "toolchains/InstallPlan.h"
#include "toolchains/Toolchains.h"
#include "ui/Paragraph.h"
#include "ui/PopupFooter.h"
#include "ui/PopupMetrics.h"
#include "ui/PopupWidgets.h"

#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
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

int changeCount(const ProjectConfig &baseline, const ProjectConfig &draft)
{
    int count = 0;
    for (const QString &family : ProjectLanguages::families()) {
        const LanguageSetup *before = baseline.language(family);
        const LanguageSetup *after = draft.language(family);
        if (!before || !after) {
            if (before != after)
                ++count;
            continue;
        }
        const QVariantMap saved = before->toVariant();
        const QVariantMap edited = after->toVariant();
        for (auto it = edited.cbegin(); it != edited.cend(); ++it) {
            if (saved.value(it.key()) != it.value())
                ++count;
        }
    }
    return count;
}

QStringList changesSummary(int changes, qsizetype downloads)
{
    QString pending = QObject::tr("No pending changes");
    if (changes == 1)
        pending = QObject::tr("1 pending change");
    else if (changes > 1)
        pending = QObject::tr("%1 pending changes").arg(changes);
    QStringList lines{pending};
    if (downloads == 1)
        lines.append(QObject::tr("1 download"));
    else if (downloads > 1)
        lines.append(QObject::tr("%1 downloads").arg(downloads));
    return lines;
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
    if (purpose_ == Purpose::Settings) {
        auto *scope = new QLabel(tr("This project"), header_);
        scope->setObjectName(QStringLiteral("pageDescription"));
        header_->addButton(scope);
    }
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
    auto *footer = new PopupFooter(this);
    summary_ = footer->hint();
    const bool isFirstSetup = purpose_ == Purpose::FirstSetup;
    cancel_ = new QPushButton(isFirstSetup ? tr("Back") : tr("Cancel"), footer);
    retry_ = new QPushButton(tr("Retry"), footer);
    skip_ = new QPushButton(tr("Continue Without Them"), footer);
    apply_ = new QPushButton(tr("Apply"), footer);
    ok_ = new QPushButton(isFirstSetup ? tr("Open Project") : tr("Save && close"), footer);
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

    for (QPushButton *button : {cancel_, retry_, skip_, apply_, ok_})
        footer->addButton(button);
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
    const QSignalBlocker blocker(form_);
    form_->setProject(project, detected);
    baseline_ = form_->project();
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
    const QList<ToolRequirement> required = RecipeLibrary::requirementsOf(project);
    const QList<InstallStep> steps = InstallPlan::plan(project, *toolchains_, required);
    QStringList lines;
    const int changes = changeCount(baseline_, project);
    if (purpose_ == Purpose::Settings) {
        lines = changesSummary(changes, steps.size());
    } else if (!steps.isEmpty()) {
        lines.append(tr("Will download: %1").arg(InstallPlan::summary(steps)));
    }
    const QStringList unavailable = InstallPlan::unavailable(project, *toolchains_, required);
    if (!unavailable.isEmpty())
        lines.append(tr("Some tools are missing"));
    Paragraph::setText(summary_, lines.isEmpty() ? tr("All tools available") : lines.join(QStringLiteral(" · ")));
    summary_->setToolTip((QStringList{InstallPlan::summary(steps)} + unavailable).join(QLatin1Char('\n')).trimmed());
    apply_->setEnabled(changes > 0 || !steps.isEmpty());
    apply_->setText(steps.isEmpty() ? tr("Apply") : tr("Download && apply"));
    if (purpose_ == Purpose::Settings)
        ok_->setText(steps.isEmpty() ? tr("Save && close") : tr("Download && close"));
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
    const QList<InstallStep> steps = InstallPlan::plan(project_, *toolchains_, RecipeLibrary::requirementsOf(project_));
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
    load(project_, ProjectDetector::detect(project_.path));
}

void ProjectSetupPanel::startDownloads(const QList<InstallStep> &steps)
{
    pages_->setCurrentIndex(1);
    Paragraph::setText(summary_, tr("Downloading %1…").arg(InstallPlan::summary(steps)));
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
    Paragraph::setText(summary_, tr("Some tools could not be downloaded; their languages "
                                    "complete with the project's words."));
    cancel_->setText(tr("Back"));
    retry_->show();
    skip_->show();
}

bool ProjectSetupPanel::isDownloading() const
{
    return provisioning_->isRunning();
}
