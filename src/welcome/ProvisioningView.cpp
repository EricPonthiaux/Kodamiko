// SPDX-License-Identifier: GPL-3.0-only

#include "ProvisioningView.h"

#include "toolchains/Installer.h"
#include "toolchains/Toolchains.h"
#include "ui/PopupWidgets.h"

#include <QDir>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QVBoxLayout>

ProvisioningView::ProvisioningView(Toolchains *toolchains, QWidget *parent)
    : QWidget(parent)
    , installer_(new Installer(toolchains, this))
    , layout_(new QVBoxLayout(this))
{
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(8);
    auto *where =
        new QLabel(tr("Into %1, shared by all projects.").arg(QDir::toNativeSeparators(toolchains->root())), this);
    where->setObjectName(QStringLiteral("pageDescription"));
    where->setWordWrap(true);
    layout_->addWidget(where);
    layout_->addStretch();

    connect(installer_, &Installer::stepStarted, this, [this](int index) {
        if (index < lines_.size())
            lines_[index].status->setText(tr("Starting…"));
    });
    connect(installer_, &Installer::progress, this, &ProvisioningView::onProgress);
    connect(installer_, &Installer::stepFinished, this, &ProvisioningView::onStepFinished);
    connect(installer_, &Installer::finished, this, &ProvisioningView::finished);
}

void ProvisioningView::start(const QList<InstallStep> &steps)
{
    steps_ = steps;
    failed_.clear();
    lines_.clear();
    delete card_;
    auto *card = new SettingsCard(this);
    card_ = card;
    for (const InstallStep &step : steps) {
        auto *control = new QWidget(card);
        auto *status = new QLabel(tr("Waiting"), control);
        status->setObjectName(QStringLiteral("settingsDescription"));
        status->setWordWrap(true);
        auto *bar = new QProgressBar(control);
        bar->setTextVisible(false);
        bar->setFixedHeight(6);
        bar->setRange(0, 100);
        bar->setValue(0);
        auto *layout = new QVBoxLayout(control);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        layout->addWidget(bar);
        layout->addWidget(status);
        control->setFixedWidth(320);
        card->addRow(step.title, step.reason, control);
        lines_.append({status, bar});
    }
    layout_->insertWidget(1, card);
    installer_->run(steps);
}

void ProvisioningView::cancel()
{
    installer_->cancel();
}

bool ProvisioningView::isRunning() const
{
    return installer_->isRunning();
}

void ProvisioningView::onProgress(int index, qint64 received, qint64 total)
{
    if (index >= lines_.size())
        return;
    const Line &line = lines_[index];
    if (total <= 0) {
        // npm: no size known.
        line.bar->setRange(0, 0);
        line.status->setText(received > 0 ? QLocale().formattedDataSize(received) : tr("Installing with npm…"));
        return;
    }
    line.bar->setRange(0, 100);
    line.bar->setValue(int(received * 100 / total));
    line.status->setText(tr("%1 of %2").arg(QLocale().formattedDataSize(received), QLocale().formattedDataSize(total)));
}

void ProvisioningView::onStepFinished(int index, const QString &error)
{
    if (index >= lines_.size())
        return;
    const Line &line = lines_[index];
    line.bar->setRange(0, 100);
    if (error.isEmpty()) {
        line.bar->setValue(100);
        line.status->setText(tr("Installed"));
        return;
    }
    line.bar->setValue(0);
    line.status->setText(error);
    line.status->setStyleSheet(QStringLiteral("color: #e06c75;"));
    failed_.append(steps_.at(index));
}
