// SPDX-License-Identifier: GPL-3.0-only

#include "ClaudeLauncher.h"

#include "ClaudeConfig.h"
#include "system/Executables.h"
#include "system/Shell.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace ClaudeLauncher {
namespace {

QString hookExecutablePath()
{
    return Executables::besideTheApplication(QStringLiteral("duet-hook"));
}

QJsonArray commandHook(const QString &command, const QString &matcher = {}, int timeout = 60)
{
    QJsonObject entry;
    if (!matcher.isEmpty())
        entry.insert(QLatin1String("matcher"), matcher);
    entry.insert(QLatin1String("hooks"), QJsonArray{QJsonObject{
                                             {QLatin1String("type"), QLatin1String("command")},
                                             {QLatin1String("command"), command},
                                             {QLatin1String("timeout"), timeout},
                                         }});
    return QJsonArray{entry};
}

// Variables a Claude Code session sets for the programs it runs. Inherited by
// `claude` (the editor launched from a Claude session), they make it a child
// of that session: hidden from the other sessions, and its transcript not
// kept. The user's own CLAUDE_CODE_* settings stay.
const QStringList inheritedSessionVariables{
    QStringLiteral("CLAUDECODE"),
    QStringLiteral("CLAUDE_PID"),
    QStringLiteral("CLAUDE_CODE_ENTRYPOINT"),
    QStringLiteral("CLAUDE_CODE_EXECPATH"),
    QStringLiteral("CLAUDE_CODE_CHILD_SESSION"),
    QStringLiteral("CLAUDE_CODE_SESSION_ID"),
    QStringLiteral("CLAUDE_CODE_SESSION_ATTENDED"),
    QStringLiteral("CLAUDE_CODE_MESSAGING_SOCKET"),
    QStringLiteral("CLAUDE_CODE_MESSAGING_TOKEN"),
    QStringLiteral("CLAUDE_EFFORT"),
    QStringLiteral("AI_AGENT"),
};

void removeInheritedSessionVariables(QProcessEnvironment &environment)
{
    for (const QString &variable : inheritedSessionVariables)
        environment.remove(variable);
}

// The sessions without Duet's tools get a file of their own: starting one
// must not rewrite the settings of a running session that has them.
QString settingsPathWithoutTools()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("claude-hooks-without-tools.json"));
}

std::optional<QString> writeSettingsFile(const QByteArray &settings, const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(settings) != settings.size() || !file.commit())
        return std::nullopt;
    return path;
}

// An npm installation provides claude.cmd, a batch file.
void makeLaunchable(ProcessSpec &spec)
{
    const ShellCommand launchable = Shell::launchable(spec.program, spec.arguments);
    spec.program = launchable.program;
    spec.arguments = launchable.arguments;
}

} // namespace

QString sessionSettingsPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("claude-hooks.json"));
}

QByteArray sessionSettings(const QString &hookCommand, const QStringList &allowedTools, GitInShell gitInShell)
{
    QJsonObject hooks;
    // The editor takes a snapshot before replying: Claude only starts once
    // the files are saved.
    hooks.insert(QLatin1String("UserPromptSubmit"), commandHook(hookCommand, {}, 30));
    // The editor refuses an edit that would overwrite unsaved changes, and a
    // git command one of its tools does.
    const QString checkedTools = gitInShell == GitInShell::Checked
                                     ? QStringLiteral("Edit|Write|MultiEdit|NotebookEdit|Bash")
                                     : QStringLiteral("Edit|Write|MultiEdit|NotebookEdit");
    hooks.insert(QLatin1String("PreToolUse"), commandHook(hookCommand, checkedTools, 30));
    hooks.insert(QLatin1String("PostToolUse"),
                 commandHook(hookCommand, QStringLiteral("Edit|Write|MultiEdit|NotebookEdit|Bash")));
    hooks.insert(QLatin1String("PermissionRequest"), commandHook(hookCommand));
    hooks.insert(QLatin1String("Notification"), commandHook(hookCommand));
    hooks.insert(QLatin1String("Stop"), commandHook(hookCommand));
    hooks.insert(QLatin1String("StopFailure"), commandHook(hookCommand));

    const QJsonObject statusLine{
        {QLatin1String("type"), QLatin1String("command")},
        {QLatin1String("command"), hookCommand + QLatin1String(" statusline")},
        {QLatin1String("padding"), 0},
    };
    QJsonObject settings{{QLatin1String("hooks"), hooks},
                         {QLatin1String("statusLine"), statusLine},
                         {QLatin1String("crossSessionInbound"), QLatin1String("accept")}};
    if (!allowedTools.isEmpty())
        settings.insert(QLatin1String("permissions"),
                        QJsonObject{{QLatin1String("allow"), QJsonArray::fromStringList(allowedTools)}});
    return QJsonDocument(settings).toJson();
}

QString mcpServerPath()
{
    return Executables::besideTheApplication(QStringLiteral("duet-mcp"));
}

QByteArray mcpConfig(const QString &mcpServer)
{
    const QJsonObject duet{{QLatin1String("type"), QLatin1String("stdio")},
                           {QLatin1String("command"), QDir::toNativeSeparators(mcpServer)},
                           {QLatin1String("args"), QJsonArray{}}};
    return QJsonDocument(QJsonObject{{QLatin1String("mcpServers"), QJsonObject{{QLatin1String("duet"), duet}}}})
        .toJson();
}

QString mcpConfigPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("claude-mcp.json"));
}

QString userStatusLineCommand(const QString &projectDir)
{
    const QString configDir = ClaudeConfig::configDir();
    // From the lowest to the highest precedence.
    const QStringList files{
        QDir(configDir).filePath(QStringLiteral("settings.json")),
        QDir(projectDir).filePath(QStringLiteral(".claude/settings.json")),
        QDir(projectDir).filePath(QStringLiteral(".claude/settings.local.json")),
    };
    QString command;
    for (const QString &path : files) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject statusLine =
            QJsonDocument::fromJson(file.readAll()).object().value(QLatin1String("statusLine")).toObject();
        if (statusLine.value(QLatin1String("type")).toString() == QLatin1String("command"))
            command = statusLine.value(QLatin1String("command")).toString();
    }
    return command;
}

// Next to the executable in the build, in share/duet once installed, in the
// bundle's resources on macOS.
QString pluginPath()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    for (const QString &candidate : {QStringLiteral("claude-plugin"), QStringLiteral("../share/duet/claude-plugin"),
                                     QStringLiteral("../Resources/claude-plugin")}) {
        const QString path = QDir::cleanPath(appDir.filePath(candidate));
        if (QFileInfo::exists(path + QStringLiteral("/.claude-plugin/plugin.json")))
            return path;
    }
    return {};
}

QString hookCommand()
{
    const QString hook = hookExecutablePath();
    // Forward slashes and quotes: the command is valid for both sh and
    // cmd.exe, even if the path contains spaces.
    return hook.isEmpty() ? QString() : QLatin1Char('"') + QDir::fromNativeSeparators(hook) + QLatin1Char('"');
}

QString findClaudeExecutable()
{
    QString inPath = QStandardPaths::findExecutable(QStringLiteral("claude"));
    if (!inPath.isEmpty())
        return inPath;

    // When launched from the desktop, the editor does not always inherit the
    // shell's PATH: try the usual installation locations.
    const QString home = QDir::homePath();
    return QStandardPaths::findExecutable(QStringLiteral("claude"), {home + QStringLiteral("/.local/bin"),
                                                                     home + QStringLiteral("/.claude/local"),
                                                                     home + QStringLiteral("/.npm-global/bin")});
}

std::optional<ProcessSpec> claudeProcess(const QString &workingDirectory,
                                         const QString &hookServerName,
                                         QString *error,
                                         const SessionOptions &options)
{
    const std::optional<QStringList> &mcpReadOnlyTools = options.mcpReadOnlyTools;
    const QString claude = findClaudeExecutable();
    if (claude.isEmpty()) {
        *error =
            QCoreApplication::translate("ClaudeLauncher", "The \"claude\" command was not found. Install Claude Code "
                                                          "from Help ▸ Check Prerequisites.");
        return std::nullopt;
    }
    const QString hook = hookCommand();
    if (hook.isEmpty()) {
        *error =
            QCoreApplication::translate("ClaudeLauncher", "The duet-hook program is missing from the editor's folder.");
        return std::nullopt;
    }

    // The editor's MCP server, when its program is there.
    const QString mcpServer = mcpReadOnlyTools ? mcpServerPath() : QString();
    std::optional<QString> mcpFile;
    if (!mcpServer.isEmpty())
        mcpFile = writeSettingsFile(mcpConfig(mcpServer), mcpConfigPath());

    const std::optional<QString> settingsFile =
        mcpFile
            ? writeSettingsFile(sessionSettings(hook, mcpReadOnlyTools.value_or(QStringList()), GitInShell::Checked),
                                sessionSettingsPath())
            : writeSettingsFile(sessionSettings(hook), settingsPathWithoutTools());
    if (!settingsFile) {
        *error = QCoreApplication::translate("ClaudeLauncher", "Could not write the hooks configuration.");
        return std::nullopt;
    }

    ProcessSpec spec;
    spec.program = claude;
    spec.arguments = QStringList{QStringLiteral("--settings"), *settingsFile};
    if (const QString plugin = pluginPath(); !plugin.isEmpty())
        spec.arguments << QStringLiteral("--plugin-dir") << QDir::toNativeSeparators(plugin);
    for (const QString &folder : options.pluginFolders)
        spec.arguments << QStringLiteral("--plugin-dir") << QDir::toNativeSeparators(folder);
    if (mcpFile)
        spec.arguments << QStringLiteral("--mcp-config") << *mcpFile;
    if (!options.name.isEmpty())
        spec.arguments << QStringLiteral("--name") << options.name;
    if (!options.sessionId.isEmpty())
        spec.arguments << QStringLiteral("--session-id") << options.sessionId;
    spec.arguments += options.extraArguments;
    spec.workingDirectory = workingDirectory;
    spec.environment = terminalEnvironment();
    spec.environment.insert(QStringLiteral("DUET_SOCKET"), hookServerName);
    // Our status line replaces the user's: duet-hook runs it and
    // displays its output.
    const QString userStatusLine = userStatusLineCommand(workingDirectory);
    if (!userStatusLine.isEmpty())
        spec.environment.insert(QStringLiteral("DUET_STATUSLINE_COMMAND"), userStatusLine);
    removeInheritedSessionVariables(spec.environment);

    makeLaunchable(spec);
    return spec;
}

std::optional<ProcessSpec> printProcess(const QString &workingDirectory, const QStringList &arguments, QString *error)
{
    const QString claude = findClaudeExecutable();
    if (claude.isEmpty()) {
        *error =
            QCoreApplication::translate("ClaudeLauncher", "The \"claude\" command was not found. Install Claude Code "
                                                          "from Help ▸ Check Prerequisites.");
        return std::nullopt;
    }
    ProcessSpec spec;
    spec.program = claude;
    spec.arguments = QStringList{QStringLiteral("--print")} + arguments;
    spec.workingDirectory = workingDirectory;
    spec.environment = QProcessEnvironment::systemEnvironment();
    removeInheritedSessionVariables(spec.environment);
    makeLaunchable(spec);
    return spec;
}

} // namespace ClaudeLauncher
