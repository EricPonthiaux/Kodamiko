// SPDX-License-Identifier: GPL-3.0-only

// MainWindow: the project's agent (see AgentProvider), and everything in the
// window that names or draws it.

#include "MainWindow.h"
#include "agent/AgentIndicator.h"
#include "agent/AgentProviders.h"
#include "agent/CloseAgentPopup.h"
#include "agent/DiagramRequestPopup.h"
#include "agent/NewAgentPopup.h"
#include "agent/PermissionPrompts.h"
#include "agent/ReviewPage.h"
#include "changes/ChangesView.h"
#include "settings/Keymap.h"
#include "settings/Preferences.h"
#include "terminal/TerminalWidget.h" // IWYU pragma: keep (TerminalWidget * -> QWidget * needs the full type)
#include "ui/MessagePopup.h"
#include "workspace/ChoicePopup.h"
#include "workspace/FileTreeView.h"
#include "workspace/FilesPanel.h"

#include <QActionGroup>
#include <QMenu>
#include <QSettings>
#include <QTabWidget>

#include <utility>

namespace {

// The name the command ids give the agent: Claude's, the only agent when
// users first changed these commands' shortcuts.
const QString stableAgentName = QStringLiteral("Claude");

} // namespace

const AgentProvider &MainWindow::projectAgentProvider() const
{
    const QVariantMap session = QSettings().value(QStringLiteral("sessions")).toMap().value(folder_).toMap();
    const QString key = session.value(QStringLiteral("agentProvider")).toString();
    return AgentProviders::find(key.isEmpty() ? Preferences::load().defaultAgentProvider : key);
}

QAction *MainWindow::nameAfterAgent(QAction *action, const QString &text, const QString &toolTip)
{
    agentNamedActions_.append({action, text, toolTip});
    Keymap::setStableName(action, QString(text).arg(stableAgentName));
    action->setText(QString(text).arg(agentProvider_->name()));
    if (!toolTip.isEmpty())
        action->setToolTip(QString(toolTip).arg(agentProvider_->name()));
    return action;
}

void MainWindow::createAgentProviderMenu(QMenu *agent)
{
    QMenu *providers = agent->addMenu(tr("A&gent"));
    agentProviderActions_ = new QActionGroup(this);
    for (const AgentProvider *provider : AgentProviders::all()) {
        QAction *action = providers->addAction(provider->name());
        action->setCheckable(true);
        action->setData(AgentProviders::key(*provider));
        agentProviderActions_->addAction(action);
        connect(action, &QAction::triggered, this, [this, provider] { chooseAgentProvider(*provider); });
    }
    providers->menuAction()->setVisible(AgentProviders::all().size() > 1);
}

void MainWindow::chooseAgentProvider(const AgentProvider &provider)
{
    if (&provider == agentProvider_)
        return;
    const auto keepCurrentChoice = [this] { setAgentProvider(*agentProvider_); };
    if (!agentSessions_.isEmpty()) {
        keepCurrentChoice();
        showStatus(tr("Close the %1 sessions in worktrees first").arg(agentProvider_->name()));
        return;
    }
    const auto switchAgent = [this, target = &provider] {
        QSettings settings;
        QVariantMap sessions = settings.value(QStringLiteral("sessions")).toMap();
        QVariantMap session = sessions.value(folder_).toMap();
        session.insert(QStringLiteral("agentProvider"), AgentProviders::key(*target));
        sessions.insert(folder_, session);
        settings.setValue(QStringLiteral("sessions"), sessions);
        setAgentProvider(*target);
        showClaudeSession();
    };
    if (agentActivity_.state() != AgentActivity::State::Working) {
        switchAgent();
        return;
    }
    keepCurrentChoice(); // until the user confirms
    auto *popup = new ChoicePopup(
        {.title = tr("Switch to %1").arg(provider.name()),
         .location = {},
         .message = tr("%1 is working. Stop it and start a %2 session?").arg(agentProvider_->name(), provider.name()),
         .details = {}},
        {{tr("Switch"), switchAgent}}, this);
    popup->popup();
}

void MainWindow::setAgentProvider(const AgentProvider &provider)
{
    agentProvider_ = &provider;
    hookServer_.setProvider(&provider);
    agentActivity_.setAgentName(provider.name());
    agentSessions_.setAgentName(provider.name());
    agentIndicator_->setProvider(&provider);
    for (const AgentNamedAction &named : std::as_const(agentNamedActions_)) {
        named.action->setText(QString(named.text).arg(provider.name()));
        if (!named.toolTip.isEmpty())
            named.action->setToolTip(QString(named.toolTip).arg(provider.name()));
    }
    for (QAction *action : agentProviderActions_->actions())
        action->setChecked(action->data().toString() == AgentProviders::key(provider));

    if (claudeTerminal_)
        terminalTabs_->setTabText(terminalTabs_->indexOf(claudeTerminal_), provider.name());
    filesPanel_->setAgentName(provider.name());
    changesView_->setAgent(provider.name(), provider.iconName());
    reviewPage_->setAgentName(provider.name());
    changesView_->applyTheme(theme_);
    for (QAction *action : std::as_const(drawDiagramActions_))
        action->setVisible(provider.features().hasFileRuns);
    fileTree_->setCanDrawDiagrams(provider.features().hasFileRuns);
    diagramRequestPopup_->setProvider(provider);
    diagramRequestPopup_->applyTheme(theme_);
    newAgentPopup_->setProvider(provider);
    newAgentPopup_->applyTheme(theme_);
    closeAgentPopup_->setIconName(provider.iconName());
    closeAgentPopup_->applyTheme(theme_);
    agentMessage_->setIconName(provider.iconName());
    agentMessage_->applyTheme(theme_);
    permissionPrompts_->setProvider(&provider);
    updateAgentIndicator();
}
