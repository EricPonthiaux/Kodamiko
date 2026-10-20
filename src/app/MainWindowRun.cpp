// SPDX-License-Identifier: GPL-3.0-only

// MainWindow: run configurations, their terminals, builds and the problems
// they report.

#include "MainWindow.h"
#include "claude/mcp/DuetTools.h"
#include "diagnostics/ProblemsView.h" // IWYU pragma: keep (ProblemsView * -> QWidget * needs the full type)
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "git/FileStatuses.h"
#include "run/RunConfiguration.h"
#include "run/RunConfigurationMenu.h"
#include "run/Runner.h"
#include "terminal/TerminalWidget.h"
#include "ui/DetachablePage.h"
#include "ui/TitleBar.h"
#include "ui/UiIcons.h"
#include "workspace/ChoicePopup.h"
#include "workspace/SidePanel.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenuBar>
#include <QStatusBar>
#include <QTimer>
#include <QWidgetAction>

#include <algorithm>
#include <ranges>

namespace {

QLatin1String runKindIconName(RunConfiguration::Kind kind)
{
    switch (kind) {
    case RunConfiguration::Kind::Build:
        return QLatin1String("build");
    case RunConfiguration::Kind::Run:
        return QLatin1String("run");
    case RunConfiguration::Kind::Debug:
        return QLatin1String("debug");
    case RunConfiguration::Kind::Test:
        return QLatin1String("test");
    }
    return QLatin1String("run");
}

// Exit code -1: stopped, no outcome color.
QColor runOutcomeColor(int exitCode)
{
    if (exitCode == -1)
        return {};
    return exitCode == 0 ? QColor("#4fa36b") : QColor("#e5534b");
}

QString runFinishedMessage(const RunConfiguration &configuration, int exitCode, int errors, int warnings)
{
    if (exitCode == -1)
        return MainWindow::tr("“%1” stopped").arg(configuration.name);
    QStringList found;
    if (errors > 0)
        found.append(MainWindow::tr("%n error(s)", nullptr, errors));
    if (warnings > 0)
        found.append(MainWindow::tr("%n warning(s)", nullptr, warnings));
    const QString foundList = found.join(QStringLiteral(", "));
    if (exitCode == 0) {
        return found.isEmpty() ? MainWindow::tr("“%1” finished").arg(configuration.name)
                               : MainWindow::tr("“%1” finished: %2").arg(configuration.name, foundList);
    }
    if (!found.isEmpty())
        return MainWindow::tr("“%1” failed: %2").arg(configuration.name, foundList);
    return MainWindow::tr("“%1” failed (exit code %2): F11 shows its output").arg(configuration.name).arg(exitCode);
}

// `sorted` by start, not empty; past the last problem, around to the first.
const Diagnostic &problemAfter(const Diagnostics &sorted, int cursor)
{
    const auto found =
        std::ranges::find_if(sorted, [cursor](const Diagnostic &problem) { return problem.start > cursor; });
    return found != sorted.end() ? *found : sorted.first();
}

// `sorted` by start, not empty; before the first problem, around to the last.
const Diagnostic &problemBefore(const Diagnostics &sorted, int cursor)
{
    const auto reversed = sorted | std::views::reverse;
    const auto found =
        std::ranges::find_if(reversed, [cursor](const Diagnostic &problem) { return problem.start < cursor; });
    return found != reversed.end() ? *found : sorted.last();
}

} // namespace

void MainWindow::createRunMenu()
{
    QMenu *run = titleBar_->menuBar()->addMenu(tr("&Run"));
    runAction_ = run->addAction(tr("&Run"), QKeySequence(tr("Shift+F10")), this, &MainWindow::runSelected);
    QMenu *runOther = run->addMenu(tr("Run &Configuration"));
    connect(runOther, &QMenu::aboutToShow, this, [this, runOther] { fillRunMenu(runOther, true); });
    run->addAction(tr("Run &Other…"), QKeySequence(tr("Alt+Shift+F10")), this, [this] {
        // The configurations under the title bar's chip.
        auto *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        fillRunMenu(menu, true);
        menu->popup(titleBar_->mapToGlobal(
            QPoint((titleBar_->width() / 2) - (menu->sizeHint().width() / 2), titleBar_->height())));
    });
    buildAction_ = run->addAction(tr("&Build Project"), QKeySequence(tr("Ctrl+F9")), this, &MainWindow::buildProject);
    stopAction_ = run->addAction(tr("&Stop"), QKeySequence(tr("Ctrl+F2")), this, &MainWindow::stopRun);
    run->addSeparator();
    run->addAction(tr("&Edit Configurations…"), this, &MainWindow::editRunConfigurations);
    run->addSeparator();
    run->addAction(tr("&Next Problem"), QKeySequence(Qt::Key_F2), this, [this] { goToProblem(true); });
    run->addAction(tr("&Previous Problem"), QKeySequence(tr("Shift+F2")), this, [this] { goToProblem(false); });

    runChooser_ = new QMenu(this);
    connect(runChooser_, &QMenu::aboutToShow, this, [this] { fillRunMenu(runChooser_, false); });
    // The configurations follow the project's files.
    connect(editorTabs_, &EditorTabs::fileSaved, this, [this](const QString &path) {
        if (QFileInfo(path) != QFileInfo(RunConfigurations::filePath(folder_)))
            return;
        QString error;
        runner_->configurations(&error);
        if (!error.isEmpty())
            showStatus(error);
        updateRunWidgets();
    });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive)
            updateRunWidgets();
    });
}

void MainWindow::fillRunMenu(QMenu *menu, bool run)
{
    menu->clear();
    QString error;
    const QList<RunConfiguration> configurations = runner_->configurations(&error);
    const std::optional<RunConfiguration> selected = runner_->selected();
    // .duet/run.json's first, then the detected ones by kind.
    const auto heading = [](const RunConfiguration &configuration) {
        if (!configuration.detected)
            return tr("Project");
        switch (configuration.kind) {
        case RunConfiguration::Kind::Build:
            return tr("Build");
        case RunConfiguration::Kind::Debug:
            return tr("Debug");
        case RunConfiguration::Kind::Test:
            return tr("Test");
        case RunConfiguration::Kind::Run:
            break;
        }
        return tr("Run");
    };
    QString previous;
    for (const RunConfiguration &configuration : configurations) {
        if (heading(configuration) != previous) {
            previous = heading(configuration);
            RunConfigurationMenu::addHeading(menu, previous, theme_);
        }
        const RunConfigurationMenu::RowState state{
            .selected = selected && selected->name == configuration.name,
            .running = runner_->isRunning(configuration.name),
        };
        const QAction *action = RunConfigurationMenu::addConfiguration(menu, configuration, state, theme_);
        connect(action, &QAction::triggered, this, [this, configuration, run] {
            runner_->select(configuration.name);
            if (run)
                runConfiguration(configuration);
        });
    }
    if (configurations.isEmpty())
        RunConfigurationMenu::addNote(menu, tr("No configuration found in the project"), theme_);
    if (!error.isEmpty())
        RunConfigurationMenu::addNote(menu, error, theme_);
    menu->addSeparator();
    menu->addAction(UiIcons::icon(QStringLiteral("settings"), theme_.foreground), tr("&Edit Configurations…"), this,
                    &MainWindow::editRunConfigurations);
}

void MainWindow::updateRunWidgets()
{
    if (!runner_ || !runAction_)
        return;
    const std::optional<RunConfiguration> selected = runner_->selected();
    const bool any = !runner_->running().isEmpty();
    stopAction_->setEnabled(any);
    runAction_->setEnabled(selected.has_value());
    buildAction_->setEnabled(runner_->buildConfiguration().has_value());
    if (!selected) {
        runAction_->setText(tr("&Run"));
        stopAction_->setText(tr("&Stop"));
        titleBar_->setRunConfiguration({
            .icon = UiIcons::icon(QStringLiteral("run"), theme_.mutedText),
            .name = tr("Add Configuration"),
            .toolTip = tr("No build file found: write the project's commands in .duet/run.json"),
            .menu = runChooser_,
            .running = false,
        });
        return;
    }
    const bool running = runner_->isRunning(selected->name);
    runAction_->setText(running ? tr("&Rerun “%1”").arg(selected->name) : tr("&Run “%1”").arg(selected->name));
    // stopRun() ends the selected configuration first.
    stopAction_->setText(running ? tr("&Stop “%1”").arg(selected->name) : tr("&Stop"));
    QString toolTip = selected->command;
    if (!selected->folder.isEmpty())
        toolTip += tr("\nin %1").arg(QDir::toNativeSeparators(selected->folder));
    if (running)
        toolTip += tr("\nRunning");
    titleBar_->setRunConfiguration({
        .icon = UiIcons::icon(runKindIconName(selected->kind), running ? QColor("#4fa36b") : theme_.foreground),
        .name = selected->name,
        .toolTip = toolTip,
        .menu = runChooser_,
        .running = running,
    });
}

void MainWindow::runSelected()
{
    if (const std::optional<RunConfiguration> configuration = runner_->selected())
        runConfiguration(*configuration);
    else
        editRunConfigurations();
}

void MainWindow::buildProject()
{
    if (const std::optional<RunConfiguration> configuration = runner_->buildConfiguration())
        runConfiguration(*configuration);
    else
        showStatus(tr("No build configuration: add one in .duet/run.json"));
}

void MainWindow::runConfiguration(const RunConfiguration &configuration)
{
    // What runs is what the editor shows.
    editorTabs_->saveAllNow();
    TerminalWidget *previous = runner_->terminal(configuration.name);
    if (previous && previous->isRunning())
        previous->terminate();

    auto *terminal = new TerminalWidget(terminalTabs_);
    terminal->applyTheme(theme_);
    int index = previous ? terminalTabs_->indexOf(previous) : -1;
    if (index >= 0) {
        terminalTabs_->removeTab(index);
        previous->deleteLater();
        index = terminalTabs_->insertTab(index, terminal, configuration.name);
    } else {
        index = terminalTabs_->addTab(terminal, configuration.name);
    }
    terminalTabs_->setTabToolTip(index, configuration.command);
    setRunTabIcon(terminal, runKindIconName(configuration.kind), {});
    // Closing the tab ends the run.
    connect(terminal, &QObject::destroyed, this,
            [this] { QTimer::singleShot(0, this, &MainWindow::updateRunWidgets); });

    // The run's terminal comes to the front, whatever its kind.
    terminalTabs_->setCurrentIndex(index);
    updateTerminalPage();
    showTerminals();
    terminal->setFocus();
    runner_->start(configuration, terminal);
}

void MainWindow::setRunTabIcon(TerminalWidget *terminal, const QString &iconName, const QColor &color)
{
    terminal->setProperty("runIcon", iconName);
    terminal->setProperty("runColor", color);
    if (const int index = terminalTabs_->indexOf(terminal); index >= 0)
        terminalTabs_->setTabIcon(index, UiIcons::icon(iconName, color.isValid() ? color : theme_.mutedText));
}

void MainWindow::onRunFinished(const RunConfiguration &configuration, int exitCode, int errors, int warnings)
{
    TerminalWidget *terminal = runner_->terminal(configuration.name);
    if (terminal)
        setRunTabIcon(terminal, runKindIconName(configuration.kind), runOutcomeColor(exitCode));
    showStatus(runFinishedMessage(configuration, exitCode, errors, warnings));

    const bool failed = exitCode != 0;
    const bool isBuildOrTest =
        configuration.kind == RunConfiguration::Kind::Build || configuration.kind == RunConfiguration::Kind::Test;
    if (exitCode != -1 && isBuildOrTest) {
        // A build or tests that went on out of sight.
        const bool hasProblemsToShow = errors > 0 || (failed && warnings > 0);
        if (mode_ == Mode::Edit && hasProblemsToShow)
            sidebar_->showView(problemsView_);
        else if (failed && terminal && mode_ != Mode::Terminal)
            terminalTabs_->setCurrentWidget(terminal); // F11 shows it
        if (!isActiveWindow())
            QApplication::alert(this);
    }
    updateRunWidgets();
}

void MainWindow::stopRun()
{
    const QStringList running = runner_->running();
    if (running.isEmpty())
        return;
    QString name = running.first();
    const std::optional<RunConfiguration> selected = runner_->selected();
    if (selected && running.contains(selected->name))
        name = selected->name;
    if (mode_ == Mode::Terminal || terminalWindow_->isDetached()) {
        for (const QString &candidate : running) {
            if (runner_->terminal(candidate) == terminalTabs_->currentWidget())
                name = candidate;
        }
    }
    runner_->stop(name);
    showStatus(tr("Stopping “%1”… (Stop again to end it at once)").arg(name));
}

void MainWindow::editRunConfigurations()
{
    const QString path = RunConfigurations::filePath(folder_);
    if (!QFileInfo::exists(path)) {
        // Started from the chosen configuration, which it then replaces.
        RunConfiguration example;
        if (const std::optional<RunConfiguration> selected = runner_->selected()) {
            example = *selected;
        } else {
            example.name = tr("Run");
            example.command = QStringLiteral("echo Hello");
        }
        example.detected = false;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            showNotice({.title = tr("Edit Configurations"),
                        .location = {},
                        .message = tr("Cannot create %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()),
                        .details = {}},
                       this);
            return;
        }
        file.write(RunConfigurations::serialize({example}));
        file.close();
        fileStatuses_->refresh();
        showStatus(tr("A configuration of .duet/run.json replaces the detected one of the same name"));
    }
    setMode(Mode::Edit);
    editorTabs_->openFile(path);
}

void MainWindow::goToProblem(bool forward)
{
    EditorWidget *editor = editorInEditMode();
    if (!editor || editor->diagnostics().isEmpty()) {
        showStatus(tr("No problems in this file"));
        return;
    }
    Diagnostics problems = editor->diagnostics();
    std::ranges::sort(problems, {}, &Diagnostic::start);
    const auto cursor = static_cast<int>(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    const Diagnostic &target = forward ? problemAfter(problems, cursor) : problemBefore(problems, cursor);
    int line = 0;
    int index = 0;
    editor->lineIndexFromPosition(target.start, &line, &index);
    editor->setCursorPosition(line, index);
    editor->ensureLineVisible(line);
    editor->setFocus();
    showStatus(target.message);
}
