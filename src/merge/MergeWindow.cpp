// SPDX-License-Identifier: GPL-3.0-only

#include "MergeWindow.h"

#include "MergeView.h"
#include "changes/GitActions.h"
#include "git/GitCommands.h"
#include "system/Host.h"
#include "ui/DesktopIntegration.h"
#include "ui/StrayWindowCheck.h"
#include "ui/ThemeStyle.h"
#include "ui/WindowFrame.h"
#include "ui/WindowTitleBar.h"
#include "workspace/ChoicePopup.h"

#include <QCloseEvent>
#include <QFileInfo>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QVBoxLayout>

MergeWindow::MergeWindow(const QString &folder, const Theme &theme, GitCommands *commands, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , commands_(commands)
    , actions_(new GitActions(
          commands,
          this,
          [folder] { return folder; },
          this))
    , view_(new MergeView(folder, theme, commands, this))
    , frame_(new WindowFrame(this))
    , titleBar_(new WindowTitleBar(this))
{
    setObjectName(QStringLiteral("mergeWindow"));
    StrayWindowCheck::allow(this);
    setWindowTitle(tr("Merge — %1 — Kodamiko").arg(QFileInfo(folder).fileName()));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(titleBar_);
    layout->addWidget(view_, 1);
    connect(view_, &MergeView::continueRequested, this, &MergeWindow::continueOperation);
    connect(view_, &MergeView::abortRequested, actions_, &GitActions::abortOperation);
    connect(view_, &MergeView::closeRequested, this, &QWidget::close);
    connect(actions_, &GitActions::changed, this, [this] {
        view_->setFinalizing(false);
        view_->reload();
        emit changed();
    });
    connect(actions_, &GitActions::integrationFinished, this, &MergeWindow::complete);
    connect(view_, &MergeView::mergeCompleted, this, &MergeWindow::complete);
    connect(actions_, &GitActions::integrationAborted, this, &MergeWindow::abandon);
    // Continuing a rebase can stop on the next commit's conflicts.
    connect(actions_, &GitActions::stoppedOnConflicts, this, &MergeWindow::showStop);
    connect(view_, &MergeView::operationEnded, this, &MergeWindow::abandon);
    const auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &QWidget::close);
    applyTheme(theme);
    const bool nativeTitleBar = Host::operatingSystem() == OperatingSystem::MacOS ||
                                QSettings().value(QStringLiteral("systemTitleBar"), false).toBool();
    setFrameless(!nativeTitleBar);
    const QSize available = screen()->availableGeometry().size();
    resize(QSize(1400, 850).boundedTo(available));
    restoreGeometry(QSettings().value(QStringLiteral("mergeWindow/geometry")).toByteArray());
}

void MergeWindow::open()
{
    setWindowState(windowState() & ~Qt::WindowMinimized);
    DesktopIntegration::followActiveSpace(this);
    show();
    raise();
    activateWindow();
    reload();
}

void MergeWindow::reload()
{
    view_->reload();
}

void MergeWindow::complete(const Integration::Completion &completion)
{
    if (ended_)
        return;
    ended_ = true;
    QSettings().setValue(QStringLiteral("mergeWindow/geometry"), saveGeometry());
    hide();
    emit finished(completion);
}

void MergeWindow::abandon()
{
    if (ended_)
        return;
    ended_ = true;
    hide();
    emit aborted();
}

void MergeWindow::applyTheme(const Theme &theme)
{
    setPalette(ThemeStyle::palette(theme));
    frame_->applyTheme(theme);
    titleBar_->applyTheme(theme);
    view_->applyTheme(theme);
}

void MergeWindow::setAgent(const AgentProvider *agent)
{
    view_->setAgent(agent);
}

void MergeWindow::setFrameless(bool frameless)
{
    frame_->setFrameless(frameless);
    titleBar_->setVisible(frameless);
}

void MergeWindow::showStop(const QString &title, const QString &report)
{
    showNotice({.title = tr("%1: conflicts to resolve").arg(title), .location = {}, .message = report, .details = {}},
               this);
}

void MergeWindow::continueOperation()
{
    if (commands_->isBusy())
        return;
    const std::optional<QString> message = view_->mergeMessage();
    view_->setFinalizing(true);
    if (message)
        actions_->finishMerge(*message);
    else
        actions_->continueOperation();
}

void MergeWindow::closeEvent(QCloseEvent *event)
{
    // Saving or committing must finish before this window can be hidden.
    if (commands_->isBusy()) {
        event->ignore();
        return;
    }
    QSettings().setValue(QStringLiteral("mergeWindow/geometry"), saveGeometry());
    QWidget::closeEvent(event);
}
