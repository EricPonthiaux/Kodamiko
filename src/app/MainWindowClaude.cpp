// SPDX-License-Identifier: GPL-3.0-only

// MainWindow: the agent's session, its hooks, the editor's MCP tools, and
// the requests sent to the agent from the editor.

#include "MainWindow.h"
#include "NativeNotifications.h"
#include "agent/AgentIndicator.h"
#include "agent/DiagramRequest.h"
#include "agent/DiagramRequestPopup.h"
#include "agent/EditErrors.h"
#include "agent/EditorContext.h"
#include "agent/PermissionPrompts.h"
#include "agent/mcp/KDKTools.h"
#include "agent/vendor/claude/ClaudeConfigView.h"
#include "agent/vendor/claude/ClaudeUsage.h"
#include "agent/vendor/claude/EffectiveSettings.h"
#include "agent/vendor/claude/GitCommandGuard.h"
#include "agent/vendor/claude/UnsavedEditGuard.h"
#include "changes/BranchesPopup.h"
#include "changes/ChangesView.h"
#include "diagnostics/ProblemStore.h"
#include "diagnostics/ProblemsView.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "git/FileStatuses.h"
#include "git/GitCommands.h"
#include "git/Snapshots.h"
#include "navigation/CodeIntelligence.h"
#include "navigation/CodeNavigator.h"
#include "terminal/TerminalWidget.h"
#include "ui/DesktopIntegration.h"
#include "ui/DetachablePage.h"
#include "ui/NoticeBar.h"
#include "ui/Snackbar.h"
#include "ui/TitleBar.h"
#include "ui/UiIcons.h"
#include "viewers/DiagramKinds.h"
#include "workspace/ChoicePopup.h"
#include "workspace/EntryPopups.h"
#include "workspace/FileTreeView.h"
#ifdef KDK_WITH_DIAGRAMS
#include "viewers/DiagramCheck.h"
#include "viewers/DiagramGenerationView.h"
#include "viewers/DiagramView.h"
#endif

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {

// Between a prompt pasted in the agent's terminal and the Enter that sends it.
constexpr int submitDelayMs = 150;

constexpr int notificationMs = 10000;

constexpr qsizetype snapshotPromptLength = 60;
// A session that exits sooner failed to start: it is not relaunched.
constexpr qint64 sessionRestartMinimumMs = 5000;

// QSettings map from a project folder to the session identifier of its
// agent's last conversation, written at each prompt: the next launch
// resumes it, however the editor closed.
const QString conversationsKey = QStringLiteral("agentConversations");

QString savedConversation(const QString &folder)
{
    return QSettings().value(conversationsKey).toMap().value(folder).toString();
}

// The session of an event works in `folder` (its root or below): an event
// still on its way from the previous project's session does not count.
bool isSessionOf(const HookEvent &event, const QString &folder)
{
    const QString directory = QDir::cleanPath(event.workingDirectory);
    return directory == folder || directory.startsWith(folder + QLatin1Char('/'));
}

void saveConversation(const QString &folder, const QString &sessionId)
{
    QSettings settings;
    QVariantMap conversations = settings.value(conversationsKey).toMap();
    if (conversations.value(folder).toString() == sessionId)
        return;
    if (sessionId.isEmpty())
        conversations.remove(folder);
    else
        conversations.insert(folder, sessionId);
    settings.setValue(conversationsKey, conversations);
}

// The indicator's state for the activity's.
AgentStatus indicatorStatus(AgentActivity::State state)
{
    switch (state) {
    case AgentActivity::State::Idle:
        return AgentStatus::Ready;
    case AgentActivity::State::Working:
        return AgentStatus::Working;
    case AgentActivity::State::NeedsInput:
        return AgentStatus::NeedsYou;
    case AgentActivity::State::Done:
        return AgentStatus::Done;
    }
    return AgentStatus::Ready;
}

// `path`, or with -2, -3… before its extension when a file already has it.
QString unusedPath(const QString &path)
{
    const QFileInfo info(path);
    QString candidate = path;
    for (int n = 2; QFileInfo::exists(candidate); ++n)
        candidate =
            info.dir().filePath(QStringLiteral("%1-%2.%3").arg(info.completeBaseName()).arg(n).arg(info.suffix()));
    return candidate;
}

// Dot drawn in front of the agents' tabs while the agent works, waits or has
// finished, in the status bar indicator's color.
QIcon activityIcon(AgentActivity::State state, const Theme &theme)
{
    if (state == AgentActivity::State::Idle)
        return {};
    const QColor color = agentStatusColor(indicatorStatus(state), theme);
    QIcon icon;
    for (const qreal ratio : {1.0, 2.0}) {
        QPixmap pixmap(QSize(16, 16) * ratio);
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QRectF(4, 4, 8, 8));
        icon.addPixmap(pixmap);
    }
    return icon;
}

// Usage of the Claude plan: red from 90%, amber from 75%, none below.
const char *usageWarningColor(double highestPercent, bool dark)
{
    if (highestPercent >= 90)
        return dark ? "#e06c75" : "#e45649";
    if (highestPercent >= 75)
        return dark ? "#e5c07b" : "#c18401";
    return nullptr;
}

} // namespace

// The agent's session lives in the first terminal tab, which cannot be
// closed. Opening a folder starts it there, replacing the previous one.
bool MainWindow::startClaudeSession(QString *error, SessionStart start)
{
    if (claudeTerminal_) {
        terminalTabs_->removeTab(terminalTabs_->indexOf(claudeTerminal_));
        claudeTerminal_->deleteLater();
        claudeTerminal_ = nullptr;
    }
    QString launchError;
    AgentSessionOptions options;
    options.conversationId = savedConversation(folder_);
    // Nothing to resume yet: the project's first conversation.
    if (start == SessionStart::Resume && options.conversationId.isEmpty())
        start = SessionStart::New;
    options.start = start;
    options.name = QFileInfo(folder_).fileName(); // the agents' sessions address it by the project's name
    if (exposeTools_->isChecked())
        options.mcpReadOnlyTools = mcpRegistry_.readOnlyToolPermissions(QStringLiteral("kodamiko"));
    addRecipesToSession(options);
    const std::optional<ProcessSpec> spec =
        agentProvider_->sessionProcess(folder_, hookServer_.serverName(), options, &launchError);
    if (!spec) {
        statusBar()->showMessage(tr("%1 session unavailable: %2").arg(agentProvider_->name(), launchError));
        if (error)
            *error = std::move(launchError);
        updateTerminalPage();
        return false;
    }
    claudeTerminal_ = new TerminalWidget(terminalTabs_);
    claudeTerminal_->applyTheme(theme_);
    claudeTerminal_->installEventFilter(this); // Escape interrupts the response, Ctrl+C too
    connect(claudeTerminal_, &TerminalWidget::finished, &agentActivity_, &AgentActivity::reset);
    // The indicator leaves the status bar with the session, even when the
    // agent was already idle.
    connect(claudeTerminal_, &TerminalWidget::finished, this, &MainWindow::updateAgentIndicator);
    connect(claudeTerminal_, &TerminalWidget::finished, this,
            [this, terminal = claudeTerminal_] { restartExitedClaudeSession(terminal); });
    agentActivity_.reset();
    claudeStatus_ = {};
    updateUsage();
    terminalTabs_->insertTab(0, claudeTerminal_, agentProvider_->name());
    const auto closeSide = static_cast<QTabBar::ButtonPosition>(
        terminalTabs_->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, terminalTabs_->tabBar()));
    terminalTabs_->tabBar()->setTabButton(0, closeSide, nullptr);
    terminalTabs_->setCurrentIndex(0);
    updateTerminalPage();
    claudeTerminal_->start(*spec);
    claudeSessionClock_.start();
    claudeSessionStart_ = start;
    if (mode_ == Mode::Terminal || terminalWindow_->isDetached())
        claudeTerminal_->setFocus();
    updateAgentIndicator();
    updateClaudeWatched();
    return true;
}

// Ctrl+Shift+L: back to the agent's session, restarted if it has exited.
void MainWindow::showClaudeSession()
{
    if (!claudeTerminal_ || !claudeTerminal_->isRunning()) {
        QString error;
        if (!startClaudeSession(&error)) {
            showNotice({.title = tr("%1 Session").arg(agentProvider_->name()),
                        .location = {},
                        .message = error,
                        .details = {}},
                       this);
            return;
        }
    }
    terminalTabs_->setCurrentWidget(claudeTerminal_);
    showTerminals();
}

// The agent reads part of its configuration only when it starts: a new
// session, which goes on with the last conversation.
void MainWindow::restartClaudeSession()
{
    if (agentActivity_.state() != AgentActivity::State::Working) {
        continueClaudeSession();
        return;
    }
    const QString agent = agentProvider_->name();
    auto *popup = new ChoicePopup({.title = tr("Restart %1 Session").arg(agent),
                                   .location = {},
                                   .message = tr("%1 is working. Stop it and restart the session?").arg(agent),
                                   .details = {}},
                                  {{tr("Restart"), [this] { continueClaudeSession(); }}}, this);
    popup->popup();
}

void MainWindow::continueClaudeSession()
{
    QString error;
    if (!startClaudeSession(&error)) {
        showNotice(
            {.title = tr("%1 Session").arg(agentProvider_->name()), .location = {}, .message = error, .details = {}},
            this);
        return;
    }
    showTerminals();
}

void MainWindow::restartExitedClaudeSession(const TerminalWidget *terminal)
{
    if (terminal != claudeTerminal_)
        return;
    const bool failedToStart = claudeSessionClock_.elapsed() < sessionRestartMinimumMs;
    if (!failedToStart) {
        startClaudeSession(); // on failure, the status bar says why
        return;
    }
    // Its conversation is gone (deleted, or of another agent): a new one.
    if (claudeSessionStart_ == SessionStart::Resume) {
        saveConversation(folder_, {});
        startClaudeSession(nullptr, SessionStart::New);
    }
}

void MainWindow::showEffectiveSettings(bool open)
{
    const QString title = tr("Effective Settings");
    if (!open && !editorTabs_->textView(title))
        return;
    QWidget *current = editorTabs_->currentWidget();
    const QList<EffectiveSettings::Layer> layers =
        EffectiveSettings::read(folder_, {}); // without Kodamiko's hooks: not for the user to see
    editorTabs_->showText(title, EffectiveSettings::render(layers, folder_), QStringLiteral("json"),
                          QStringLiteral("settings"));
    if (open)
        setMode(Mode::Edit);
    else
        editorTabs_->setCurrentWidget(current); // updated in the background
}

void MainWindow::onHookReceived(const HookEvent &event)
{
    permissionPrompts_->handle(event, askerOf(event));
    // An agent edits its worktree, not the checkout the editor shows.
    if (handleAgentHook(event))
        return;
    agentActivity_.handle(event);
    switch (event.kind) {
    case HookEvent::Kind::PromptSubmitted:
        // From the prompt on, the conversation is saved and can be resumed.
        if (isSessionOf(event, folder_))
            saveConversation(folder_, event.sessionId);
        shareEditorContext();
        snapshotBeforePrompt(event.prompt);
        claudeTouched_.clear(); // the dots show the last prompt's files
        updateClaudeMarks();
        break;
    case HookEvent::Kind::EditRequested:
        guardUnsavedEdits(event.filePaths);
        editErrorCheck_->rememberErrorsBefore(event.filePaths);
        break;
    case HookEvent::Kind::CommandRequested:
        guardGitCommands(event);
        break;
    case HookEvent::Kind::FilesEdited:
    case HookEvent::Kind::CommandFinished: {
        if (event.kind == HookEvent::Kind::FilesEdited) {
            followDiagramEdits(event.filePaths);
            reportEditErrors(event.filePaths);
            recentFiles_.markEditedByClaude(event.filePaths);
            changesView_->markEditedByClaude(event.filePaths);
            for (const QString &path : event.filePaths)
                claudeTouched_.insert(QDir::cleanPath(path));
            updateClaudeMarks();
        }
        fileStatuses_->refresh();
        claudeConfigView_->scheduleRefresh();
        // A command may have changed any file. Once the hook has its reply:
        // a conflict's question must not keep the agent waiting.
        const QStringList paths =
            event.kind == HookEvent::Kind::FilesEdited ? event.filePaths : editorTabs_->openFilePaths();
        editorTabs_->syncWithDiskLater(paths);
        refreshIndexes();
        if (mode_ == Mode::Diff)
            changesView_->refresh();
        break;
    }
    case HookEvent::Kind::ResponseFinished:
        onRepositoryChanged();
        showStatus(
            (event.failed ? tr("%1 stopped on an error") : tr("%1 response finished")).arg(agentProvider_->name()));
        break;
    case HookEvent::Kind::StatusUpdated:
        claudeStatus_ = event.status;
        updateUsage();
        break;
    case HookEvent::Kind::PermissionRequested:
    case HookEvent::Kind::NeedsAttention:
        break;
    }
}

// How a permission request names the session it comes from.
QString MainWindow::askerOf(const HookEvent &event) const
{
    const AgentSessions::Agent *agent = agentSessions_.find(event);
    return agent ? tr("%1 in %2").arg(agentProvider_->name(), agent->worktree.name) : agentProvider_->name();
}

void MainWindow::updateAgentIndicator()
{
    const AgentActivity::State state = agentActivity_.state();
    const QIcon icon = activityIcon(state, theme_);
    const QString description = agentActivity_.description();
    // The activity dot replaces the tab's icon while the agent works, waits
    // or has finished.
    const bool idle = state == AgentActivity::State::Idle;
    if (claudeTerminal_) {
        const int index = terminalTabs_->indexOf(claudeTerminal_);
        terminalTabs_->setTabIcon(index, idle ? UiIcons::icon(agentProvider_->iconName(), agentProvider_->brandColor())
                                              : icon);
        terminalTabs_->setTabToolTip(index, description);
    }
    modeTabs_->setTabIcon(0, idle ? UiIcons::icon(QStringLiteral("terminal"), theme_.mutedText) : icon);
    modeTabs_->updateGeometry(); // see updateTabIcons()
    const bool hasSession = claudeTerminal_ && claudeTerminal_->isRunning();
    // Activity updates must not uncover the indicator over a status message.
    agentIndicator_->setVisible(hasSession && statusBar()->currentMessage().isEmpty());
    if (hasSession)
        agentIndicator_->setStatus(indicatorStatus(state));
    agentIndicator_->setToolTip(description + QLatin1Char('\n') +
                                tr("Show the %1 session (Ctrl+Shift+L)").arg(agentProvider_->name()));
    const QString modeHint = tr("Full-screen terminals (Ctrl+1)");
    modeTabs_->setTabToolTip(0, state == AgentActivity::State::Idle ? modeHint
                                                                    : description + QLatin1Char('\n') + modeHint);
}

void MainWindow::updateAgentTab(QWidget *terminal, AgentActivity::State state)
{
    const int index = terminalTabs_->indexOf(terminal);
    if (index < 0)
        return;
    const bool idle = state == AgentActivity::State::Idle;
    terminalTabs_->setTabIcon(index, idle ? UiIcons::icon(agentProvider_->iconName(), agentProvider_->brandColor())
                                          : activityIcon(state, theme_));
}

void MainWindow::updateClaudeWatched()
{
    // In the main window or in the terminals' own: the window it is in.
    const bool terminalsShown = mode_ == Mode::Terminal || terminalWindow_->isDetached();
    agentActivity_.setWatched(claudeTerminal_ && claudeTerminal_->isActiveWindow() && terminalsShown &&
                              terminalTabs_->currentWidget() == claudeTerminal_);
    const QWidget *current = terminalTabs_->currentWidget();
    agentSessions_.setWatchedTerminal(current && current->isActiveWindow() && terminalsShown ? current : nullptr);
}

void MainWindow::notify(const QString &title, const QString &message)
{
    if (!claudeNotifications_->isChecked())
        return;
    QApplication::alert(this);
    // In the window, the dot on the tabs is enough.
    const bool isKDKActive = isActiveWindow() || (claudeTerminal_ && claudeTerminal_->isActiveWindow());
    if (isKDKActive)
        return;
    qApp->setBadgeNumber(1); // cleared when the window comes back
    const QPointer<MainWindow> window(this);
    const auto clicked = [window] {
        if (window)
            window->bringClaudeSessionToFront();
    };
    if (NativeNotifications::show(title, message, clicked))
        return;
    showTrayNotification(title, message);
}

void MainWindow::showTrayNotification(const QString &title, const QString &message)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable() || !QSystemTrayIcon::supportsMessages())
        return;
    if (!trayIcon_) {
        QIcon icon = QApplication::windowIcon();
        if (icon.isNull())
            icon = style()->standardIcon(QStyle::SP_MessageBoxInformation);
        trayIcon_ = new QSystemTrayIcon(icon, this);
        trayIcon_->setToolTip(QStringLiteral("Kodamiko"));
        connect(trayIcon_, &QSystemTrayIcon::messageClicked, this, &MainWindow::bringClaudeSessionToFront);
        connect(trayIcon_, &QSystemTrayIcon::activated, this, &MainWindow::bringClaudeSessionToFront);
    }
    trayIcon_->show();
    trayIcon_->showMessage(title, message, QSystemTrayIcon::Information, notificationMs);
}

void MainWindow::bringClaudeSessionToFront()
{
    showNormal();
    raise();
    activateWindow();
    showClaudeSession();
}

void MainWindow::keepAwakeWhileAgentWorks()
{
    if (agentActivity_.state() == AgentActivity::State::Working)
        DesktopIntegration::beginBackgroundActivity(tr("%1 is working").arg(agentProvider_->name()));
    else
        DesktopIntegration::endBackgroundActivity();
}

// The window is back: what called the user to it goes.
void MainWindow::clearNotifications()
{
    if (trayIcon_)
        trayIcon_->hide();
    NativeNotifications::clear();
    qApp->setBadgeNumber(0);
}

void MainWindow::registerMcpTools()
{
    KDKToolContext context;
    context.folder = [this] { return folder_; };
    context.tabs = editorTabs_;
    context.navigator = navigator_;
    context.symbols = symbolIndex_;
    context.languageServers = languageServers_;
    context.problems = problemStore_;
    context.intelligence = intelligence_;
    context.git = gitCommands_;
    context.menus = titleBar_->menuBar();
    context.showMessage = [this](const QString &message) { statusBar()->showMessage(message, 5000); };
    context.terminals = [this] {
        QList<std::pair<QString, TerminalWidget *>> terminals;
        for (int i = 0; i < terminalTabs_->count(); ++i) {
            if (auto *terminal = qobject_cast<TerminalWidget *>(terminalTabs_->widget(i)))
                terminals.append({terminalTabs_->tabText(i), terminal});
        }
        return terminals;
    };
    context.addTerminal = [this](const QString &title, const ProcessSpec &spec) { return addTerminal(title, spec); };
    context.showChanges = [this](const QString &relativePath) {
        setMode(Mode::Diff);
        changesView_->refresh();
        if (!relativePath.isEmpty())
            changesView_->selectFile(relativePath);
    };
    context.setCommitMessage = [this](const QString &message) {
        setMode(Mode::Diff);
        changesView_->showCommitMessage(message);
    };
    context.repositoryChanged = [this] { onRepositoryChanged(); };
    KDKTools::registerEditorTools(mcpRegistry_, context);
    KDKTools::registerWorkspaceTools(mcpRegistry_, context);
    KDKTools::registerCodeTools(mcpRegistry_, context);
    KDKTools::registerRefactoringTools(mcpRegistry_, context);
    KDKTools::registerTerminalTools(mcpRegistry_, context);
    KDKTools::registerGitTools(mcpRegistry_, context);
#ifdef KDK_WITH_DIAGRAMS
    KDKTools::registerDiagramTools(mcpRegistry_, context);
#endif
    mcpRegistry_.setConfirmer(
        [this](const QString &tool, const QString &what, const std::function<void(bool)> &answer) {
            askToolConfirmation(tool, what, answer);
        });
    hookServer_.setMcpRegistry(&mcpRegistry_);

    // Subscribed clients hear when the user moves in the editor (once
    // things settle) and when the repository changes.
    auto *editorMoved = new QTimer(this);
    editorMoved->setSingleShot(true);
    editorMoved->setInterval(400);
    connect(editorMoved, &QTimer::timeout, this,
            [this] { mcpRegistry_.notifyResourceUpdated(QStringLiteral("kodamiko://editor/state")); });
    connect(editorTabs_, &QTabWidget::currentChanged, editorMoved, qOverload<>(&QTimer::start));
    connect(editorTabs_, &EditorTabs::editorOpened, this, [editorMoved](EditorWidget *editor) {
        connect(editor, &QsciScintilla::cursorPositionChanged, editorMoved, qOverload<>(&QTimer::start));
        connect(editor, &QsciScintilla::selectionChanged, editorMoved, qOverload<>(&QTimer::start));
        connect(editor, &QsciScintilla::modificationChanged, editorMoved, qOverload<>(&QTimer::start));
    });
}

// A sensitive tool of Kodamiko's (restore a snapshot, commit…) the agent wants
// to run. Closing the message refuses it, as Deny does.
void MainWindow::askToolConfirmation(const QString &tool, const QString &what, const std::function<void(bool)> &answer)
{
    // The snackbar runs one of the three: the answer is given once.
    snackbar_->post({
        .icon = UiIcons::icon(agentProvider_->iconName(), agentProvider_->brandColor()),
        .title = tr("%1 asks to %2").arg(agentProvider_->name(), what),
        .detail = tool,
        .actions = {{tr("Deny"), [answer] { answer(false); }},
                    {tr("Allow"), [answer] { answer(true); }, SnackbarAction::Emphasis::Primary}},
        .closeToolTip = tr("Deny"),
        .dismissed = [answer] { answer(false); },
    });
    QApplication::alert(this);
}

void MainWindow::shareEditorContext()
{
    if (!shareEditorContext_->isChecked())
        return;
    const EditorContext context = EditorContext::of(*editorTabs_, folder_, recentFiles_.opened());
    const QString description = context.describe();
    if (!description.isEmpty())
        hookServer_.respond(agentProvider_->promptContextReply(description));
}

void MainWindow::guardGitCommands(const HookEvent &event)
{
    // Only the sessions with Kodamiko's tools send their commands (see
    // ClaudeLauncher::GitInShell).
    if (!gitThroughKDKTools_->isChecked())
        return;
    const std::optional<QString> reason =
        GitCommandGuard::refusal(event.message, {.projectFolder = folder_, .workingDirectory = event.workingDirectory});
    if (!reason)
        return;
    hookServer_.respond(agentProvider_->denyToolReply(*reason));
    showStatus(tr("%1 was sent to Kodamiko's git tools instead of running git").arg(agentProvider_->name()));
}

void MainWindow::guardUnsavedEdits(const QStringList &paths)
{
    QStringList unsaved;
    for (const QString &path : paths) {
        if (editorTabs_->hasUnsavedChangesUnder(path)) {
            const QString relative = QDir(folder_).relativeFilePath(path);
            unsaved.append(relative.startsWith(QLatin1String("..")) ? path : relative);
        }
    }
    if (unsaved.isEmpty())
        return;
    hookServer_.respond(agentProvider_->denyToolReply(UnsavedEditGuard::reason(unsaved)));
    const QString files = unsaved.join(QStringLiteral(", "));
    const QString agent = agentProvider_->name();
    unsavedEditNotice_->showNotice(
        tr("%1 did not edit %2: it has unsaved changes here. Save or discard them, then ask %1 again.")
            .arg(agent, files),
        tr("Save All"));
    showStatus(tr("Edit by %1 refused, unsaved changes in %2").arg(agent, files));
}

void MainWindow::reportEditErrors(const QStringList &paths)
{
    if (!reportEditErrors_->isChecked())
        return;
    // The agent waits for the report before going on, as it would for a
    // compiler.
    editErrorCheck_->checkAfterEdit(
        paths, [respond = hookServer_.deferResponse(), provider = agentProvider_](const QString &report) {
            respond(report.isEmpty() ? QJsonObject() : provider->editContextReply(report));
        });
}

void MainWindow::followDiagramEdits(const QStringList &paths)
{
#ifdef KDK_WITH_DIAGRAMS
    for (const QString &path : paths) {
        if (!DiagramView::isDiagramFile(path))
            continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QStringList problems = DiagramCheck::problems(path, file.readAll());
        if (!problems.isEmpty()) {
            const QString relative = QDir(folder_).relativeFilePath(path);
            hookServer_.respond(agentProvider_->blockReply(DiagramCheck::feedback(relative, problems)));
            return;
        }
        // A diagram the agent has just drawn shows up, laid out.
        if (!editorTabs_->diagramViewFor(path))
            editorTabs_->openFile(path);
    }
#else
    Q_UNUSED(paths);
#endif
}

void MainWindow::generateDiagram(const QString &folder)
{
    const QList<DiagramKind> kinds = DiagramKinds::available();
    if (kinds.isEmpty() || folder.isEmpty() || !agentProvider_->features().hasFileRuns)
        return;
    const QString relative = QDir(folder_).relativeFilePath(folder);
    // At the root, diagrams go with the documentation.
    const QString initialName = relative == QLatin1String(".") ? QStringLiteral("docs/diagrams/") : QString();
    const QString location = relative == QLatin1String(".") ? QFileInfo(folder_).fileName() + QLatin1Char('/')
                                                            : QDir::toNativeSeparators(relative + QLatin1Char('/'));
    diagramRequestPopup_->popup(
        {location, kinds, initialName},
        [this, folder](const QString &kindId, const QString &name, const QString &description) {
            const DiagramKind kind = DiagramKinds::find(kindId);
            if (description.isEmpty())
                return tr("Describe what the diagram should show");
            if (name.isEmpty() || name.endsWith(QLatin1Char('/')) || name.endsWith(QLatin1Char('\\')))
                return tr("Enter a file name");
            const EntryTarget target = resolveNewEntry(folder_, folder, DiagramKinds::withExtension(name, kind), false);
            if (!target.error.isEmpty())
                return target.error;
            drawDiagram(target.path, kind, description);
            return QString();
        });
}

void MainWindow::drawDiagram(const QString &path, const DiagramKind &kind, const QString &description)
{
#ifdef KDK_WITH_DIAGRAMS
    setMode(Mode::Edit);
    auto *view = new DiagramGenerationView({folder_, path, kind.id, kind.title, description, agentProvider_});
    editorTabs_->showPage(view, QFileInfo(path).fileName(), agentProvider_->iconName());
    connect(view, &DiagramGenerationView::closeRequested, this, [this, view] { editorTabs_->closePage(view); });
    connect(view, &DiagramGenerationView::finished, this, [this, view](const QString &drawn, const QString &summary) {
        editorTabs_->replacePage(view, drawn);
        showStatus(summary.isEmpty() ? tr("Diagram drawn by %1").arg(agentProvider_->name())
                                     : summary.section(QLatin1Char('\n'), 0, 0));
    });
    view->start();
#else
    Q_UNUSED(path);
    Q_UNUSED(kind);
    Q_UNUSED(description);
#endif
}

void MainWindow::snapshotBeforePrompt(const QString &prompt)
{
    if (!snapshotBeforePrompt_->isChecked())
        return;
    QString summary = prompt.section(QLatin1Char('\n'), 0, 0).simplified();
    if (summary.size() > snapshotPromptLength)
        summary = summary.left(snapshotPromptLength - 1) + QStringLiteral("…");
    const QString agent = agentProvider_->name();
    const QString reason = summary.isEmpty() ? tr("Before a %1 prompt").arg(agent)
                                             : tr("Before the %1 prompt \u201c%2\u201d").arg(agent, summary);
    // A dozen git commands in a large repository: they run off the UI
    // thread, and the agent waits for the snapshot before touching any file.
    gitCommands_->runQuietly(
        [folder = folder_, reason, taken = tr("Snapshot taken before the prompt")] {
            const std::optional<RepositoryInfo> info = RepositoryInfo::read(folder);
            if (!info || Snapshots::createOfWorkingTree(*info, reason).isEmpty())
                return QString();
            return taken;
        },
        [this, reply = hookServer_.deferReply()](const QString &message, const QString &error) {
            reply();
            if (!error.isEmpty())
                showStatus(tr("Snapshot before the prompt failed: %1").arg(error));
            else if (!message.isEmpty())
                showStatus(message);
        });
}

void MainWindow::updateUsage()
{
    const ClaudeUsage usage = ClaudeUsage::fromStatusJson(claudeStatus_);
    const QString summary = usage.summary();
    usageLabel_->setVisible(!summary.isEmpty());
    usageLabel_->setToolTip(usage.details());
    const char *warning = usageWarningColor(usage.highestPercent(), theme_.isDark);
    usageLabel_->setText(
        warning
            ? QStringLiteral("<span style=\"color:%1\">%2</span>").arg(QLatin1String(warning), summary.toHtmlEscaped())
            : summary);
}

void MainWindow::updateClaudeMarks()
{
    fileTree_->setClaudeTouched(claudeTouched_);
    claudeConfigView_->setClaudeTouched(claudeTouched_);
}

void MainWindow::sendToClaude(const QString &path)
{
    QString relative = QDir(folder_).relativeFilePath(path);
    if (relative.startsWith(QLatin1String(".."))) // outside the project (~/.claude...)
        relative = path;
    if (relative == QLatin1String("."))
        relative = QStringLiteral("./");
    else if (QFileInfo(path).isDir())
        relative += QLatin1Char('/');
    // A file mention in Claude Code's prompt, typed where the cursor is.
    typeInClaudePrompt(QStringLiteral("@%1 ").arg(QDir::toNativeSeparators(relative)), PromptEntry::Typed);
}

void MainWindow::createCodeRequestActions(QMenu *claude)
{
    const auto add = [this, claude](const QString &text, const QKeySequence &shortcut, auto slot) {
        QAction *action = claude->addAction(text, shortcut, this, slot);
        // Only while the editor has the focus: in the file tree, Ctrl+Alt+K
        // sends the selected file instead.
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        editorTabs_->addAction(action);
        codeRequestActions_.append(action);
    };
    add(tr("&Explain This Code"), {}, [this] { askClaude(CodeRequest::Kind::Explain); });
    add(tr("Explain With a F&lowchart"), {}, [this] { explainWithFlowchart(); });
    add(tr("Re&view This Code"), {}, [this] { reviewCode(); });
    add(tr("Write T&ests for This Code"), {}, [this] { askClaude(CodeRequest::Kind::WriteTests); });
    add(tr("Ask %1 to &Fix the Problems Here"), {}, [this] { askClaudeToFixProblemsHere(); });
    nameAfterAgent(codeRequestActions_.last(), tr("Ask %1 to &Fix the Problems Here"));
    intelligence_->setProblemFixAction(codeRequestActions_.last());
    add(tr("&Add to %1's Prompt"), QKeySequence(tr("Ctrl+Alt+K")), [this] { addCodeToClaudePrompt(); });
    nameAfterAgent(codeRequestActions_.last(), tr("&Add to %1's Prompt"));

    auto *fixSelected = new QAction(this);
    connect(fixSelected, &QAction::triggered, this, [this] { askClaudeToFix(problemsView_->selectedProblems()); });
    nameAfterAgent(fixSelected, tr("Ask %1 to &Fix"));
    problemsView_->setContextActions({fixSelected});
}

void MainWindow::askClaude(CodeRequest::Kind kind)
{
    if (const std::optional<CodeRequest::Target> target = currentCodeTarget())
        typeInClaudePrompt(CodeRequest::prompt(kind, *target), PromptEntry::Submitted);
}

void MainWindow::explainWithFlowchart()
{
    const DiagramKind flowchart = DiagramKinds::find(QStringLiteral("flowchart"));
    if (flowchart.id.isEmpty() || !agentProvider_->features().hasFileRuns) {
        showStatus(tr("%1 cannot draw diagrams in this version of Kodamiko").arg(agentProvider_->name()));
        return;
    }
    const std::optional<CodeRequest::Target> target = currentCodeTarget();
    if (!target)
        return;
    const QString name = DiagramRequest::explanationFileName(target->symbol, target->path, flowchart.extension);
    const QString path = unusedPath(QDir(folder_).filePath(QStringLiteral("docs/diagrams/") + name));
    const QString language = QLocale::languageToString(QLocale::system().language());
    drawDiagram(path, flowchart, DiagramRequest::explanation(CodeRequest::describe(*target), language));
    if (target->unsaved)
        showStatus(
            tr("%1 reads %2 as saved: its unsaved changes are left out").arg(agentProvider_->name(), target->path));
}

void MainWindow::askClaudeToFixProblemsHere()
{
    const std::optional<CodeRequest::Target> target = currentCodeTarget();
    if (!target)
        return;
    const QString path = editorTabs_->currentEditor()->filePath();
    Problems here;
    for (const Problem &problem : problemStore_->problems(path)) {
        if (problem.line + 1 >= target->firstLine && problem.line + 1 <= target->lastLine)
            here.append(problem);
    }
    if (here.isEmpty()) {
        showStatus(target->firstLine == target->lastLine ? tr("No problem on this line")
                                                         : tr("No problem on the selected lines"));
        return;
    }
    askClaudeToFix(here);
}

void MainWindow::askClaudeToFix(const Problems &problems)
{
    if (problems.isEmpty())
        return;
    const bool unsaved = std::ranges::any_of(
        problems, [this](const Problem &problem) { return editorTabs_->hasUnsavedChangesUnder(problem.path); });
    typeInClaudePrompt(CodeRequest::fixPrompt(problems, folder_, unsaved), PromptEntry::Submitted);
}

void MainWindow::addCodeToClaudePrompt()
{
    if (const std::optional<CodeRequest::Target> target = currentCodeTarget())
        typeInClaudePrompt(CodeRequest::mention(*target) + QLatin1Char(' '), PromptEntry::Typed);
}

std::optional<CodeRequest::Target> MainWindow::currentCodeTarget()
{
    const EditorWidget *editor = editorTabs_->currentEditor();
    std::optional<CodeRequest::Target> target = editor ? CodeRequest::targetIn(*editor, folder_) : std::nullopt;
    if (!target)
        showStatus(tr("Open a file to ask %1 about its code").arg(agentProvider_->name()));
    return target;
}

void MainWindow::typeInClaudePrompt(const QString &text, PromptEntry entry)
{
    if (!claudeTerminal_ || !claudeTerminal_->isRunning()) {
        QString error;
        if (!startClaudeSession(&error)) {
            showNotice({.title = tr("%1 Session").arg(agentProvider_->name()),
                        .location = {},
                        .message = error,
                        .details = {}},
                       this);
            return;
        }
    }
    claudeTerminal_->paste(text);
    if (entry == PromptEntry::Submitted) {
        // Apart from the paste: an Enter read with the pasted text may be
        // taken for a new line in the prompt.
        QTimer::singleShot(submitDelayMs, claudeTerminal_, &TerminalWidget::pressEnter);
    }
    terminalTabs_->setCurrentWidget(claudeTerminal_);
    showTerminals();
}
