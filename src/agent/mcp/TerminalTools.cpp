// SPDX-License-Identifier: GPL-3.0-only

// MCP tools for terminals: what they show, and commands run in a tab the
// user sees (unlike Claude's own shell, which runs hidden).

#include "KDKTools.h"
#include "McpRegistry.h"
#include "system/Shell.h"
#include "terminal/ProcessSpec.h"
#include "terminal/TerminalWidget.h"

#include <QJsonArray>
#include <QPointer>
#include <QTimer>

#include <memory>

namespace KDKTools {

namespace {

using namespace McpSchema;

constexpr int defaultLines = 100;
constexpr int maximumLines = 2000;
constexpr int outputLines = 200;
constexpr int progressMs = 3000;
constexpr int defaultTimeoutSeconds = 300;
// Without waiting: the first lines the command prints, shortly after it starts.
constexpr int noWaitMs = 500;

using TerminalTabs = QList<std::pair<QString, TerminalWidget *>>;

// How a run_in_terminal call ends.
enum class RunEnd {
    Finished,     // the command exited
    StillRunning, // the timeout passed first
    Closed,       // the user closed its tab, which stops it
};
// Answers a run_in_terminal call, with the exit code if it finished.
using Answer = std::function<void(RunEnd end, int exitCode)>;

// A run_in_terminal call waiting for its answer. The terminal's last lines
// are kept as it runs: a tab the user closes is deleted without a word.
struct PendingRun {
    bool answered = false;
    QString output;
};

// The command run by the user's shell, then the shell exits.
ProcessSpec commandSpec(const QString &folder, const QString &command)
{
    ProcessSpec spec = defaultShell(folder);
    const ShellCommand shell = Shell::userShellRunning(command);
    spec.program = shell.program;
    spec.arguments = shell.arguments;
    return spec;
}

TerminalTabs terminalTabs(const KDKToolContext &context)
{
    return context.terminals ? context.terminals() : TerminalTabs();
}

McpResult listTerminals(const KDKToolContext &context, const QJsonObject &)
{
    QJsonArray terminals;
    const TerminalTabs tabs = terminalTabs(context);
    for (qsizetype i = 0; i < tabs.size(); ++i)
        terminals.append(QJsonObject{{QStringLiteral("id"), int(i)},
                                     {QStringLiteral("title"), tabs[i].first},
                                     {QStringLiteral("running"), tabs[i].second->isRunning()},
                                     {QStringLiteral("claudeSession"), i == 0}});
    return McpResult::json(QJsonObject{{QStringLiteral("terminals"), terminals}});
}

McpResult readTerminal(const KDKToolContext &context, const QJsonObject &arguments)
{
    const TerminalTabs tabs = terminalTabs(context);
    const int id = arguments.value(QStringLiteral("id")).toInt(-1);
    if (id < 0 || id >= tabs.size())
        return McpResult::error(QStringLiteral("No terminal %1 (see list_terminals)").arg(id));
    const int count = std::clamp(arguments.value(QStringLiteral("lines")).toInt(defaultLines), 1, maximumLines);
    return McpResult::json(
        QJsonObject{{QStringLiteral("title"), tabs[id].first},
                    {QStringLiteral("running"), tabs[id].second->isRunning()},
                    {QStringLiteral("text"), tabs[id].second->lastLines(count).join(QLatin1Char('\n'))}});
}

QString describeRun(const QJsonObject &arguments)
{
    return QStringLiteral("run “%1” in a terminal").arg(arguments.value(QStringLiteral("command")).toString());
}

// Replies once with what `terminal` shows: when the command ends, at the
// timeout, or when its tab is closed, whichever comes first.
Answer answerOnce(TerminalWidget *terminal, const std::shared_ptr<PendingRun> &run, const McpReply &reply)
{
    const QPointer<TerminalWidget> guard(terminal);
    return [guard, run, reply](RunEnd end, int exitCode) {
        if (run->answered)
            return;
        run->answered = true;
        // A closed tab's terminal is being destroyed: only what was kept is left.
        const bool isShown = guard && end != RunEnd::Closed;
        const QString output = isShown ? guard->lastLines(outputLines).join(QLatin1Char('\n')) : run->output;
        QJsonObject result{{QStringLiteral("finished"), end == RunEnd::Finished}, {QStringLiteral("output"), output}};
        if (end == RunEnd::Finished)
            result.insert(QStringLiteral("exitCode"), exitCode);
        if (end == RunEnd::Closed)
            result.insert(QStringLiteral("closedByUser"), true);
        reply(McpResult::json(result));
    };
}

// Reports the terminal's last line every few seconds, keeping its output for
// a closed tab, until the call is answered; answers unfinished once
// `timeoutSeconds` have passed.
void watchUntilTimeout(TerminalWidget *terminal,
                       int timeoutSeconds,
                       const std::shared_ptr<PendingRun> &run,
                       const McpProgress &progress,
                       const Answer &answer)
{
    auto *ticker = new QTimer(terminal);
    ticker->setInterval(progressMs);
    const auto started = std::make_shared<int>(0);
    QObject::connect(ticker, &QTimer::timeout, terminal,
                     [terminal, ticker, started, timeoutSeconds, run, progress, answer] {
                         // Answered as the command ended: no progress for a
                         // finished request, which MCP forbids.
                         if (run->answered) {
                             ticker->deleteLater();
                             return;
                         }
                         *started += progressMs;
                         if (*started >= timeoutSeconds * 1000) {
                             ticker->stop();
                             answer(RunEnd::StillRunning, 0);
                             return;
                         }
                         run->output = terminal->lastLines(outputLines).join(QLatin1Char('\n'));
                         progress(*started / 1000.0, timeoutSeconds, terminal->lastLines(1).value(0));
                     });
    ticker->start();
}

void runInTerminal(const KDKToolContext &context,
                   const QJsonObject &arguments,
                   const McpReply &reply,
                   const McpProgress &progress)
{
    if (!context.addTerminal) {
        reply(McpResult::error(QStringLiteral("Kodamiko cannot open terminals here")));
        return;
    }
    const QString command = arguments.value(QStringLiteral("command")).toString();
    const QString title = arguments.value(QStringLiteral("title")).toString(command.section(QLatin1Char(' '), 0, 0));
    TerminalWidget *terminal = context.addTerminal(title, commandSpec(context.folder(), command));
    if (!terminal) {
        reply(McpResult::error(QStringLiteral("The terminal could not start")));
        return;
    }
    const int timeoutSeconds =
        std::max(0, arguments.value(QStringLiteral("timeoutSeconds")).toInt(defaultTimeoutSeconds));
    const auto run = std::make_shared<PendingRun>();
    const Answer answer = answerOnce(terminal, run, reply);
    QObject::connect(terminal, &TerminalWidget::finished, terminal,
                     [answer](int exitCode) { answer(RunEnd::Finished, exitCode); });
    // Closing the tab deletes the terminal without emitting finished, and
    // the timers go with it: without this, the call would never be answered.
    QObject::connect(terminal, &QObject::destroyed, [answer] { answer(RunEnd::Closed, 0); });
    if (timeoutSeconds == 0) {
        QTimer::singleShot(noWaitMs, terminal, [answer] { answer(RunEnd::StillRunning, 0); });
        return;
    }
    watchUntilTimeout(terminal, timeoutSeconds, run, progress, answer);
}

} // namespace

void registerTerminalTools(McpRegistry &registry, const KDKToolContext &context)
{
    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("list_terminals"),
         .title = QStringLiteral("Terminals"),
         .description = QStringLiteral("The terminal tabs of Kodamiko (the first is this Claude session), with whether "
                                       "their program still runs: what read_terminal can read."),
         .inputSchema = object({})},
        handledBy(context, listTerminals)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("read_terminal"),
         .title = QStringLiteral("Read a terminal"),
         .description = QStringLiteral("The last lines a terminal tab shows (scrollback included): a build's errors, "
                                       "a server's log, a test run the user started."),
         .inputSchema =
             object({{QStringLiteral("id"), integer(QStringLiteral("Terminal id (see list_terminals)"))},
                     {QStringLiteral("lines"), integer(QStringLiteral("How many lines, from the end (default 100)"))}},
                    {QStringLiteral("id")})},
        handledBy(context, readTerminal)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("run_in_terminal"),
         .title = QStringLiteral("Run in a terminal"),
         .description = QStringLiteral(
             "Runs a shell command in a new Kodamiko terminal tab the user watches (a dev server, a long build, an "
             "interactive tool), and returns its exit code and last lines once it ends, or what it printed so far "
             "after the timeout (it keeps running), or closedByUser if the user closed its tab, which stops it. The "
             "user is asked first. For quick commands whose output only "
             "you need, use your own shell instead."),
         .inputSchema =
             object({{QStringLiteral("command"), string(QStringLiteral("Shell command, run in the project's folder"))},
                     {QStringLiteral("title"), string(QStringLiteral("Tab title (optional)"))},
                     {QStringLiteral("timeoutSeconds"), integer(QStringLiteral("How long to wait for it to end "
                                                                               "(default 300; 0: do not wait)"))}},
                    {QStringLiteral("command")})},
        describeRun, [context](const QJsonObject &arguments, const McpReply &reply, const McpProgress &progress) {
            runInTerminal(context, arguments, reply, progress);
        }));
}

} // namespace KDKTools
