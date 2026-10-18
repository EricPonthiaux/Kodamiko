// MCP tools for terminals: what they show, and commands run in a tab the
// user sees (unlike Claude's own shell, which runs hidden).

#include "DuetTools.h"
#include "McpRegistry.h"

#include "terminal/ProcessSpec.h"
#include "terminal/TerminalWidget.h"

#include <QJsonArray>
#include <QPointer>
#include <QTimer>

#include <memory>

namespace DuetTools {

namespace {

using namespace McpSchema;

constexpr int defaultLines = 100;
constexpr int maximumLines = 2000;
constexpr int outputLines = 200;
constexpr int progressMs = 3000;

// The command run by the user's shell, then the shell exits.
ProcessSpec commandSpec(const QString &folder, const QString &command)
{
    ProcessSpec spec = defaultShell(folder);
#ifdef Q_OS_WIN
    spec.arguments = {QStringLiteral("-NoLogo"), QStringLiteral("-Command"), command};
#else
    spec.arguments = {QStringLiteral("-c"), command};
#endif
    return spec;
}

} // namespace

void registerTerminalTools(McpRegistry &registry, const DuetToolContext &context)
{
    registry.addTool(McpTool::reading(
        QStringLiteral("list_terminals"), QStringLiteral("Terminals"),
        QStringLiteral("The terminal tabs of Duet (the first is this Claude session), with whether their program "
                       "still runs: what read_terminal can read."),
        object({}), McpTool::immediate([context](const QJsonObject &) {
            QJsonArray terminals;
            const auto tabs = context.terminals ? context.terminals() : QList<std::pair<QString, TerminalWidget *>>();
            for (qsizetype i = 0; i < tabs.size(); ++i)
                terminals.append(QJsonObject{{QStringLiteral("id"), int(i)},
                                             {QStringLiteral("title"), tabs[i].first},
                                             {QStringLiteral("running"), tabs[i].second->isRunning()},
                                             {QStringLiteral("claudeSession"), i == 0}});
            return McpResult::json(QJsonObject{{QStringLiteral("terminals"), terminals}});
        })));

    registry.addTool(McpTool::reading(
        QStringLiteral("read_terminal"), QStringLiteral("Read a terminal"),
        QStringLiteral("The last lines a terminal tab shows (scrollback included): a build's errors, a server's "
                       "log, a test run the user started."),
        object({{QStringLiteral("id"), integer(QStringLiteral("Terminal id (see list_terminals)"))},
                {QStringLiteral("lines"), integer(QStringLiteral("How many lines, from the end (default 100)"))}},
               {QStringLiteral("id")}),
        McpTool::immediate([context](const QJsonObject &arguments) {
            const auto tabs = context.terminals ? context.terminals() : QList<std::pair<QString, TerminalWidget *>>();
            const int id = arguments.value(QStringLiteral("id")).toInt(-1);
            if (id < 0 || id >= tabs.size())
                return McpResult::error(QStringLiteral("No terminal %1 (see list_terminals)").arg(id));
            const int count = std::clamp(arguments.value(QStringLiteral("lines")).toInt(defaultLines), 1, maximumLines);
            return McpResult::json(
                QJsonObject{{QStringLiteral("title"), tabs[id].first},
                            {QStringLiteral("running"), tabs[id].second->isRunning()},
                            {QStringLiteral("text"), tabs[id].second->lastLines(count).join(QLatin1Char('\n'))}});
        })));

    registry.addTool(McpTool::sensitive(
        QStringLiteral("run_in_terminal"), QStringLiteral("Run in a terminal"),
        QStringLiteral("Runs a shell command in a new Duet terminal tab the user watches (a dev server, a long "
                       "build, an interactive tool), and returns its exit code and last lines once it ends, or "
                       "what it printed so far after the timeout (it keeps running). The user is asked first. "
                       "For quick commands whose output only you need, use your own shell instead."),
        object({{QStringLiteral("command"), string(QStringLiteral("Shell command, run in the project's folder"))},
                {QStringLiteral("title"), string(QStringLiteral("Tab title (optional)"))},
                {QStringLiteral("timeoutSeconds"), integer(QStringLiteral("How long to wait for it to end "
                                                                          "(default 300; 0: do not wait)"))}},
               {QStringLiteral("command")}),
        [](const QJsonObject &arguments) {
            return QStringLiteral("run “%1” in a terminal").arg(arguments.value(QStringLiteral("command")).toString());
        },
        [context](const QJsonObject &arguments, McpReply reply, McpProgress progress) {
            if (!context.addTerminal) {
                reply(McpResult::error(QStringLiteral("Duet cannot open terminals here")));
                return;
            }
            const QString command = arguments.value(QStringLiteral("command")).toString();
            const QString title =
                arguments.value(QStringLiteral("title")).toString(command.section(QLatin1Char(' '), 0, 0));
            TerminalWidget *terminal = context.addTerminal(title, commandSpec(context.folder(), command));
            if (!terminal) {
                reply(McpResult::error(QStringLiteral("The terminal could not start")));
                return;
            }
            const int timeout = std::max(0, arguments.value(QStringLiteral("timeoutSeconds")).toInt(300));
            const QPointer<TerminalWidget> guard(terminal);
            // Answered once: when it ends, or at the timeout.
            auto answered = std::make_shared<bool>(false);
            const auto answer = [guard, reply, answered](bool finished, int exitCode) {
                if (*answered)
                    return;
                *answered = true;
                QJsonObject result{{QStringLiteral("finished"), finished},
                                   {QStringLiteral("output"),
                                    guard ? guard->lastLines(outputLines).join(QLatin1Char('\n')) : QString()}};
                if (finished)
                    result.insert(QStringLiteral("exitCode"), exitCode);
                reply(McpResult::json(result));
            };
            QObject::connect(terminal, &TerminalWidget::finished, terminal,
                             [answer](int exitCode) { answer(true, exitCode); });
            if (timeout == 0) {
                QTimer::singleShot(500, terminal, [answer] { answer(false, 0); });
                return;
            }
            auto *ticker = new QTimer(terminal);
            ticker->setInterval(progressMs);
            const auto started = std::make_shared<int>(0);
            QObject::connect(ticker, &QTimer::timeout, terminal, [guard, ticker, started, timeout, progress, answer] {
                *started += progressMs;
                if (*started >= timeout * 1000) {
                    ticker->stop();
                    answer(false, 0);
                    return;
                }
                const QStringList last = guard ? guard->lastLines(1) : QStringList();
                progress(*started / 1000.0, timeout, last.value(0));
            });
            ticker->start();
        }));
}

} // namespace DuetTools
