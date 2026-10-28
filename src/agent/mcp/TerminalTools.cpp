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
// Answers a run_in_terminal call: finished or not, and the exit code if so.
using Answer = std::function<void(bool finished, int exitCode)>;

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

// Replies once with what `terminal` shows: when the command ends, or at the
// timeout, whichever comes first.
Answer answerOnce(TerminalWidget *terminal, const McpReply &reply)
{
    const QPointer<TerminalWidget> guard(terminal);
    const auto answered = std::make_shared<bool>(false);
    return [guard, reply, answered](bool finished, int exitCode) {
        if (*answered)
            return;
        *answered = true;
        QJsonObject result{
            {QStringLiteral("finished"), finished},
            {QStringLiteral("output"), guard ? guard->lastLines(outputLines).join(QLatin1Char('\n')) : QString()}};
        if (finished)
            result.insert(QStringLiteral("exitCode"), exitCode);
        reply(McpResult::json(result));
    };
}

// Reports the terminal's last line every few seconds, and answers unfinished
// once `timeoutSeconds` have passed.
void watchUntilTimeout(TerminalWidget *terminal, int timeoutSeconds, const McpProgress &progress, const Answer &answer)
{
    const QPointer<TerminalWidget> guard(terminal);
    auto *ticker = new QTimer(terminal);
    ticker->setInterval(progressMs);
    const auto started = std::make_shared<int>(0);
    QObject::connect(ticker, &QTimer::timeout, terminal, [guard, ticker, started, timeoutSeconds, progress, answer] {
        *started += progressMs;
        if (*started >= timeoutSeconds * 1000) {
            ticker->stop();
            answer(false, 0);
            return;
        }
        const QStringList last = guard ? guard->lastLines(1) : QStringList();
        progress(*started / 1000.0, timeoutSeconds, last.value(0));
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
    const Answer answer = answerOnce(terminal, reply);
    QObject::connect(terminal, &TerminalWidget::finished, terminal, [answer](int exitCode) { answer(true, exitCode); });
    if (timeoutSeconds == 0) {
        QTimer::singleShot(noWaitMs, terminal, [answer] { answer(false, 0); });
        return;
    }
    watchUntilTimeout(terminal, timeoutSeconds, progress, answer);
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
             "after the timeout (it keeps running). The user is asked first. For quick commands whose output only "
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
