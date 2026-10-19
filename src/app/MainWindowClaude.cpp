// MainWindow: the Claude session, its hooks, the editor's MCP tools, and
// the requests sent to Claude from the editor.

#include "MainWindow.h"
#include "changes/BranchesPopup.h"
#include "changes/ChangesView.h"
#include "claude/ClaudeConfigView.h"
#include "claude/ClaudeLauncher.h"
#include "claude/ClaudeUsage.h"
#include "claude/DiagramRequestPopup.h"
#include "claude/EditorContext.h"
#include "claude/EffectiveSettings.h"
#include "claude/UnsavedEditGuard.h"
#include "claude/mcp/DuetTools.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "git/FileStatuses.h"
#include "git/GitCommands.h"
#include "git/Snapshots.h"
#include "navigation/SymbolIndex.h"
#include "terminal/TerminalWidget.h"
#include "ui/DetachablePage.h"
#include "ui/NoticeBar.h"
#include "ui/TitleBar.h"
#include "ui/UiIcons.h"
#include "viewers/DiagramKinds.h"
#include "workspace/FileTreeView.h"
#ifdef DUET_WITH_DIAGRAMS
#include "viewers/DiagramCheck.h"
#include "viewers/DiagramGenerationView.h"
#include "viewers/DiagramView.h"
#endif

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>

namespace {

// Between a prompt pasted in Claude's terminal and the Enter that sends it.
constexpr int submitDelayMs = 150;

constexpr int notificationMs = 10000;

constexpr qsizetype snapshotPromptLength = 60;

// The indicator's state for the activity's.
ClaudeIndicator::Status indicatorStatus(ClaudeActivity::State state)
{
    switch (state) {
    case ClaudeActivity::State::Idle:
        return ClaudeIndicator::Status::Ready;
    case ClaudeActivity::State::Working:
        return ClaudeIndicator::Status::Working;
    case ClaudeActivity::State::NeedsInput:
        return ClaudeIndicator::Status::NeedsYou;
    case ClaudeActivity::State::Done:
        return ClaudeIndicator::Status::Done;
    }
    return ClaudeIndicator::Status::Ready;
}

// Dot drawn in front of the Claude tabs while Claude works, waits or has
// finished, in the title bar indicator's color.
QIcon activityIcon(ClaudeActivity::State state, const Theme &theme)
{
    if (state == ClaudeActivity::State::Idle)
        return {};
    const QColor color = claudeStatusColor(indicatorStatus(state), theme);
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

// The Claude session lives in the first terminal tab, which cannot be
// closed. Opening a folder starts it there, replacing the previous one.
bool MainWindow::startClaudeSession(QString *error, const QStringList &extraArguments)
{
    if (claudeTerminal_) {
        terminalTabs_->removeTab(terminalTabs_->indexOf(claudeTerminal_));
        claudeTerminal_->deleteLater();
        claudeTerminal_ = nullptr;
    }
    QString launchError;
    ClaudeLauncher::SessionOptions options;
    options.extraArguments = extraArguments;
    if (exposeTools_->isChecked())
        options.mcpReadOnlyTools = mcpRegistry_.readOnlyToolPermissions(QStringLiteral("duet"));
    const std::optional<ProcessSpec> spec =
        ClaudeLauncher::claudeProcess(folder_, hookServer_.serverName(), &launchError, options);
    if (!spec) {
        statusBar()->showMessage(tr("Claude session unavailable: %1").arg(launchError));
        if (error)
            *error = std::move(launchError);
        updateTerminalPage();
        return false;
    }
    claudeTerminal_ = new TerminalWidget(terminalTabs_);
    claudeTerminal_->applyTheme(theme_);
    claudeTerminal_->installEventFilter(this); // Escape interrupts the response, Ctrl+C too
    connect(claudeTerminal_, &TerminalWidget::finished, &claudeActivity_, &ClaudeActivity::reset);
    // The indicator leaves the title bar with the session, even when Claude
    // was already idle.
    connect(claudeTerminal_, &TerminalWidget::finished, this, &MainWindow::updateClaudeIndicator);
    claudeActivity_.reset();
    claudeStatus_ = {};
    updateUsage();
    terminalTabs_->insertTab(0, claudeTerminal_, QStringLiteral("Claude"));
    const auto closeSide = static_cast<QTabBar::ButtonPosition>(
        terminalTabs_->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, terminalTabs_->tabBar()));
    terminalTabs_->tabBar()->setTabButton(0, closeSide, nullptr);
    terminalTabs_->setCurrentIndex(0);
    updateTerminalPage();
    claudeTerminal_->start(*spec);
    if (mode_ == Mode::Terminal || terminalWindow_->isDetached())
        claudeTerminal_->setFocus();
    updateClaudeIndicator();
    updateClaudeWatched();
    return true;
}

// Ctrl+Shift+L: back to the Claude session, restarted if it has exited.
void MainWindow::showClaudeSession()
{
    if (!claudeTerminal_ || !claudeTerminal_->isRunning()) {
        QString error;
        if (!startClaudeSession(&error)) {
            QMessageBox::warning(this, tr("Claude Session"), error);
            return;
        }
    }
    terminalTabs_->setCurrentWidget(claudeTerminal_);
    showTerminals();
}

// Claude reads part of its configuration only when it starts: a new
// session, which goes on with the last conversation.
void MainWindow::restartClaudeSession()
{
    if (claudeActivity_.state() == ClaudeActivity::State::Working &&
        QMessageBox::question(this, tr("Restart Claude Session"),
                              tr("Claude is working. Stop it and restart the session?")) != QMessageBox::Yes)
        return;
    QString error;
    if (!startClaudeSession(&error, {QStringLiteral("--continue")})) {
        QMessageBox::warning(this, tr("Claude Session"), error);
        return;
    }
    showTerminals();
}

void MainWindow::showEffectiveSettings(bool open)
{
    const QString title = tr("Effective Settings");
    if (!open && !editorTabs_->textView(title))
        return;
    QWidget *current = editorTabs_->currentWidget();
    const QList<EffectiveSettings::Layer> layers =
        EffectiveSettings::read(folder_, {}); // without Duet's hooks: not for the user to see
    editorTabs_->showText(title, EffectiveSettings::render(layers, folder_), QStringLiteral("json"),
                          QStringLiteral("settings"));
    if (open)
        setMode(Mode::Edit);
    else
        editorTabs_->setCurrentWidget(current); // updated in the background
}

void MainWindow::onHookReceived(const HookEvent &event)
{
    claudeActivity_.handle(event);
    switch (event.kind) {
    case HookEvent::Kind::PromptSubmitted:
        shareEditorContext();
        snapshotBeforePrompt(event.prompt);
        claudeTouched_.clear(); // the dots show the last prompt's files
        updateClaudeMarks();
        break;
    case HookEvent::Kind::EditRequested:
        guardUnsavedEdits(event.filePaths);
        break;
    case HookEvent::Kind::FilesEdited:
    case HookEvent::Kind::CommandFinished: {
        if (event.kind == HookEvent::Kind::FilesEdited) {
            followDiagramEdits(event.filePaths);
            recentFiles_.markEditedByClaude(event.filePaths);
            for (const QString &path : event.filePaths)
                claudeTouched_.insert(QDir::cleanPath(path));
            updateClaudeMarks();
        }
        fileStatuses_->refresh();
        claudeConfigView_->scheduleRefresh();
        // A command may have changed any file.
        const QStringList paths =
            event.kind == HookEvent::Kind::FilesEdited ? event.filePaths : editorTabs_->openFilePaths();
        for (const QString &path : paths)
            editorTabs_->syncWithDisk(path);
        symbolIndex_->refresh();
        if (mode_ == Mode::Diff)
            changesView_->refresh();
        break;
    }
    case HookEvent::Kind::ResponseFinished:
        onRepositoryChanged();
        showStatus(event.failed ? tr("Claude stopped on an error") : tr("Claude response finished"));
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

void MainWindow::updateClaudeIndicator()
{
    const ClaudeActivity::State state = claudeActivity_.state();
    const QIcon icon = activityIcon(state, theme_);
    const QString description = claudeActivity_.description();
    // The activity dot replaces the tab's icon while Claude works, waits or
    // has finished.
    const bool idle = state == ClaudeActivity::State::Idle;
    if (claudeTerminal_) {
        const int index = terminalTabs_->indexOf(claudeTerminal_);
        terminalTabs_->setTabIcon(index, idle ? UiIcons::icon(QStringLiteral("claude"), QColor("#d97757")) : icon);
        terminalTabs_->setTabToolTip(index, description);
    }
    modeTabs_->setTabIcon(0, idle ? UiIcons::icon(QStringLiteral("terminal"), theme_.mutedText) : icon);
    modeTabs_->updateGeometry(); // see updateTabIcons()
    const bool hasSession = claudeTerminal_ && claudeTerminal_->isRunning();
    titleBar_->setClaudeState(hasSession ? std::optional(indicatorStatus(state)) : std::nullopt,
                              description + QLatin1Char('\n') + tr("Show the Claude session (Ctrl+Shift+L)"));
    const QString modeHint = tr("Full-screen terminals (Ctrl+1)");
    modeTabs_->setTabToolTip(0, state == ClaudeActivity::State::Idle ? modeHint
                                                                     : description + QLatin1Char('\n') + modeHint);
}

void MainWindow::updateClaudeWatched()
{
    // In the main window or in the terminals' own: the window it is in.
    const bool terminalsShown = mode_ == Mode::Terminal || terminalWindow_->isDetached();
    claudeActivity_.setWatched(claudeTerminal_ && claudeTerminal_->isActiveWindow() && terminalsShown &&
                               terminalTabs_->currentWidget() == claudeTerminal_);
}

void MainWindow::notify(const QString &title, const QString &message)
{
    if (!claudeNotifications_->isChecked())
        return;
    QApplication::alert(this);
    // In the window, the dot on the tabs is enough.
    const bool isDuetActive = isActiveWindow() || (claudeTerminal_ && claudeTerminal_->isActiveWindow());
    if (isDuetActive || !QSystemTrayIcon::isSystemTrayAvailable() || !QSystemTrayIcon::supportsMessages())
        return;
    if (!trayIcon_) {
        QIcon icon = QApplication::windowIcon();
        if (icon.isNull())
            icon = style()->standardIcon(QStyle::SP_MessageBoxInformation);
        trayIcon_ = new QSystemTrayIcon(icon, this);
        trayIcon_->setToolTip(QStringLiteral("Duet"));
        const auto showSession = [this] {
            showNormal();
            raise();
            activateWindow();
            showClaudeSession();
        };
        connect(trayIcon_, &QSystemTrayIcon::messageClicked, this, showSession);
        connect(trayIcon_, &QSystemTrayIcon::activated, this, showSession);
    }
    trayIcon_->show();
    trayIcon_->showMessage(title, message, QSystemTrayIcon::Information, notificationMs);
}

void MainWindow::registerMcpTools()
{
    DuetToolContext context;
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
    context.repositoryChanged = [this] { onRepositoryChanged(); };
    DuetTools::registerEditorTools(mcpRegistry_, context);
    DuetTools::registerWorkspaceTools(mcpRegistry_, context);
    DuetTools::registerCodeTools(mcpRegistry_, context);
    DuetTools::registerTerminalTools(mcpRegistry_, context);
    DuetTools::registerGitTools(mcpRegistry_, context);
#ifdef DUET_WITH_DIAGRAMS
    DuetTools::registerDiagramTools(mcpRegistry_, context);
#endif
    mcpRegistry_.setConfirmer([this](const QString &tool, const QString &what, std::function<void(bool)> answer) {
        toolRequests_.append({tr("Claude asks to %1 (%2).").arg(what, tool), std::move(answer)});
        if (toolRequests_.size() == 1)
            showToolRequest();
    });
    hookServer_.setMcpRegistry(&mcpRegistry_);

    // Subscribed clients hear when the user moves in the editor (once
    // things settle) and when the repository changes.
    auto *editorMoved = new QTimer(this);
    editorMoved->setSingleShot(true);
    editorMoved->setInterval(400);
    connect(editorMoved, &QTimer::timeout, this,
            [this] { mcpRegistry_.notifyResourceUpdated(QStringLiteral("duet://editor/state")); });
    connect(editorTabs_, &QTabWidget::currentChanged, editorMoved, qOverload<>(&QTimer::start));
    connect(editorTabs_, &EditorTabs::editorOpened, this, [editorMoved](EditorWidget *editor) {
        connect(editor, &QsciScintilla::cursorPositionChanged, editorMoved, qOverload<>(&QTimer::start));
        connect(editor, &QsciScintilla::selectionChanged, editorMoved, qOverload<>(&QTimer::start));
        connect(editor, &QsciScintilla::modificationChanged, editorMoved, qOverload<>(&QTimer::start));
    });
}

void MainWindow::showToolRequest()
{
    if (toolRequests_.isEmpty())
        return;
    toolRequestBar_->showNotice(toolRequests_.first().text, tr("Allow"));
    // Close refuses; the request waits for an answer.
    QApplication::alert(this);
}

void MainWindow::shareEditorContext()
{
    if (!shareEditorContext_->isChecked())
        return;
    const EditorContext context = EditorContext::of(*editorTabs_, folder_);
    const QString description = context.describe();
    if (!description.isEmpty())
        hookServer_.respond(EditorContext::promptHookOutput(description));
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
    hookServer_.respond(UnsavedEditGuard::denyOutput(UnsavedEditGuard::reason(unsaved)));
    const QString files = unsaved.join(QStringLiteral(", "));
    unsavedEditNotice_->showNotice(
        tr("Claude did not edit %1: it has unsaved changes here. Save or discard them, then ask Claude again.")
            .arg(files),
        tr("Save All"));
    showStatus(tr("Edit by Claude refused, unsaved changes in %1").arg(files));
}

void MainWindow::followDiagramEdits(const QStringList &paths)
{
#ifdef DUET_WITH_DIAGRAMS
    for (const QString &path : paths) {
        if (!DiagramView::isDiagramFile(path))
            continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QStringList problems = DiagramCheck::problems(path, file.readAll());
        if (!problems.isEmpty()) {
            const QString relative = QDir(folder_).relativeFilePath(path);
            hookServer_.respond(DiagramCheck::feedbackOutput(relative, problems));
            return;
        }
        // A diagram Claude has just drawn shows up, laid out.
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
    if (kinds.isEmpty() || folder.isEmpty())
        return;
    const QString relative = QDir(folder_).relativeFilePath(folder);
    // At the root, diagrams go with the documentation.
    const QString initialName = relative == QLatin1String(".") ? QStringLiteral("docs/diagrams/") : QString();
    const QString location = relative == QLatin1String(".") ? QFileInfo(folder_).fileName() + QLatin1Char('/')
                                                            : QDir::toNativeSeparators(relative + QLatin1Char('/'));
    auto *popup = new DiagramRequestPopup(
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
            drawDiagram(target.path, kind.title, description);
            return QString();
        },
        this);
    popup->popup();
}

void MainWindow::drawDiagram(const QString &path, const QString &kindTitle, const QString &description)
{
#ifdef DUET_WITH_DIAGRAMS
    setMode(Mode::Edit);
    auto *view = new DiagramGenerationView({folder_, path, kindTitle, description});
    editorTabs_->showPage(view, QFileInfo(path).fileName(), QStringLiteral("claude"));
    connect(view, &DiagramGenerationView::closeRequested, this, [this, view] { editorTabs_->closePage(view); });
    connect(view, &DiagramGenerationView::finished, this, [this, view](const QString &drawn, const QString &summary) {
        editorTabs_->replacePage(view, drawn);
        showStatus(summary.isEmpty() ? tr("Diagram drawn by Claude") : summary.section(QLatin1Char('\n'), 0, 0));
    });
    view->start();
#else
    Q_UNUSED(path);
    Q_UNUSED(kindTitle);
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
    const QString reason =
        summary.isEmpty() ? tr("Before a Claude prompt") : tr("Before the Claude prompt \u201c%1\u201d").arg(summary);
    // A dozen git commands in a large repository: they run off the UI
    // thread, and Claude waits for the snapshot before touching any file.
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
    add(tr("Re&view This Code"), {}, [this] { askClaude(CodeRequest::Kind::Review); });
    add(tr("Write T&ests for This Code"), {}, [this] { askClaude(CodeRequest::Kind::WriteTests); });
    add(tr("&Add to Claude's Prompt"), QKeySequence(tr("Ctrl+Alt+K")), [this] { addCodeToClaudePrompt(); });
}

void MainWindow::askClaude(CodeRequest::Kind kind)
{
    if (const std::optional<CodeRequest::Target> target = currentCodeTarget())
        typeInClaudePrompt(CodeRequest::prompt(kind, *target), PromptEntry::Submitted);
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
        showStatus(tr("Open a file to ask Claude about its code"));
    return target;
}

void MainWindow::typeInClaudePrompt(const QString &text, PromptEntry entry)
{
    if (!claudeTerminal_ || !claudeTerminal_->isRunning()) {
        QString error;
        if (!startClaudeSession(&error)) {
            QMessageBox::warning(this, tr("Claude Session"), error);
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
