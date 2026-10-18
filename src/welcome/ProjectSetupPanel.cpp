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

ProjectSetupPanel::ProjectSetupPanel(Toolchains *toolchains, const QString &actionText, QWidget *parent)
    : QWidget(parent)
    , toolchains_(toolchains)
    , header_(new PopupHeader(QStringLiteral("files"), QString(), this))
    , pages_(new QStackedWidget(this))
    , form_(new ProjectSetupForm(toolchains, this))
    , provisioning_(new ProvisioningView(toolchains, this))
    , summary_(new QLabel(this))
    , back_(new QPushButton(tr("Exit"), this))
    , detect_(new QPushButton(tr("Detect Again"), this))
    , retry_(new QPushButton(tr("Retry"), this))
    , skip_(new QPushButton(tr("Continue Without Them"), this))
    , action_(new QPushButton(actionText, this))
{
    // The header: the project, and where it is (see load()).
    connect(header_, &PopupHeader::closeRequested, this, [this] {
        if (provisioning_->isRunning())
            provisioning_->cancel();
        emit cancelled();
    });

    // The pages: the languages, then the downloads.
    const auto scrolled = [this](QWidget *content) {
        auto *holder = new QWidget;
        holder->setObjectName(QStringLiteral("pageContent"));
        auto *layout = new QVBoxLayout(holder);
        layout->setContentsMargins(16, 12, 16, 16);
        layout->addWidget(content);
        auto *scroll = new QScrollArea(this);
        scroll->setObjectName(QStringLiteral("pageScroll"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(holder);
        return scroll;
    };
    pages_->addWidget(scrolled(form_));
    pages_->addWidget(scrolled(provisioning_));
    connect(form_, &ProjectSetupForm::changed, this, &ProjectSetupPanel::updateSummary);
    connect(provisioning_, &ProvisioningView::finished, this, &ProjectSetupPanel::onDownloadsFinished);

    // The footer.
    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("popupFooter"));
    summary_->setObjectName(QStringLiteral("footerHint"));
    summary_->setWordWrap(true);
    action_->setDefault(true);
    connect(back_, &QPushButton::clicked, this, [this] {
        if (provisioning_->isRunning()) {
            provisioning_->cancel();
            showForm();
            return;
        }
        if (pages_->currentIndex() == 1) {
            showForm();
            return;
        }
        emit cancelled();
    });
    connect(detect_, &QPushButton::clicked, form_, &ProjectSetupForm::detectAgain);
    connect(retry_, &QPushButton::clicked, this, [this] { startDownloads(provisioning_->failedSteps()); });
    connect(skip_, &QPushButton::clicked, this, [this] { emit done(project_); });
    connect(action_, &QPushButton::clicked, this, &ProjectSetupPanel::apply);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 10, 16, 10);
    footerLayout->setSpacing(8);
    footerLayout->addWidget(summary_, 1);
    footerLayout->addWidget(back_);
    footerLayout->addWidget(detect_);
    footerLayout->addWidget(retry_);
    footerLayout->addWidget(skip_);
    footerLayout->addWidget(action_);
    for (QPushButton *button : {back_, detect_, retry_, skip_, action_}) {
        button->setParent(footer);
        button->setMinimumWidth(84);
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addWidget(pages_, 1);
    layout->addWidget(footer);
}

void ProjectSetupPanel::applyTheme(const Theme &theme)
{
    theme_ = theme;
    header_->applyTheme(theme);
}

void ProjectSetupPanel::load(const ProjectConfig &project, const QList<DetectedLanguage> &detected)
{
    project_ = project;
    header_->setTitle(project.configured ? tr("%1 — Project Settings").arg(project.name)
                                         : tr("Set Up %1").arg(project.name));
    header_->setSubtitle(QDir::toNativeSeparators(project.path));
    form_->setProject(project, detected);
    showForm();
}

void ProjectSetupPanel::showForm()
{
    pages_->setCurrentIndex(0);
    back_->setText(tr("Back"));
    back_->show();
    detect_->show();
    retry_->hide();
    skip_->hide();
    action_->show();
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
    summary_->setText(lines.isEmpty() ? tr("Everything the project needs is here.") : lines.join(QLatin1Char('\n')));
}

void ProjectSetupPanel::apply()
{
    if (pages_->currentIndex() != 0)
        return;
    download(form_->project());
}

void ProjectSetupPanel::download(const ProjectConfig &project)
{
    project_ = project;
    // The setup is kept even if the downloads fail.
    ProjectStore::save(project_);
    const QList<InstallStep> steps = InstallPlan::plan(project_, *toolchains_);
    if (steps.isEmpty()) {
        emit done(project_);
        return;
    }
    header_->setTitle(tr("Downloading the Tools of %1").arg(project_.name));
    header_->setSubtitle(QDir::toNativeSeparators(project_.path));
    startDownloads(steps);
}

void ProjectSetupPanel::startDownloads(const QList<InstallStep> &steps)
{
    pages_->setCurrentIndex(1);
    summary_->setText(tr("Downloading %1…").arg(InstallPlan::summary(steps)));
    back_->setText(tr("Cancel"));
    back_->show();
    detect_->hide();
    retry_->hide();
    skip_->hide();
    action_->hide();
    provisioning_->start(steps);
}

void ProjectSetupPanel::onDownloadsFinished(bool ok)
{
    if (ok) {
        emit done(project_);
        return;
    }
    if (pages_->currentIndex() != 1 || provisioning_->failedSteps().isEmpty())
        return; // cancelled
    summary_->setText(tr("Some tools could not be downloaded; their languages "
                         "complete with the project's words."));
    back_->setText(tr("Back"));
    retry_->show();
    skip_->show();
}

bool ProjectSetupPanel::isDownloading() const
{
    return provisioning_->isRunning();
}
