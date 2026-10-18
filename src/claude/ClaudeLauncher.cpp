#include "ClaudeLauncher.h"

#include "ClaudeConfig.h"

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

// A program shipped next to the editor, or an empty string.
QString companionPath(const QString &program)
{
#ifdef Q_OS_WIN
    const QString name = program + QStringLiteral(".exe");
#else
    const QString &name = program;
#endif
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(name);
    return QFileInfo::exists(path) ? path : QString();
}

QString hookExecutablePath()
{
    return companionPath(QStringLiteral("duet-hook"));
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

std::optional<QString> writeSettingsFile(const QByteArray &settings, const QString &path = sessionSettingsPath())
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(settings) != settings.size() || !file.commit())
        return std::nullopt;
    return path;
}

// An npm installation provides claude.cmd, which CreateProcess can only
// launch through cmd.exe.
void launchThroughCmd(const ProcessSpec &spec)
{
#ifdef Q_OS_WIN
    if (spec.program.endsWith(QLatin1String(".cmd"), Qt::CaseInsensitive) ||
        spec.program.endsWith(QLatin1String(".bat"), Qt::CaseInsensitive)) {
        spec.arguments.prepend(QDir::toNativeSeparators(spec.program));
        spec.arguments.prepend(QStringLiteral("/c"));
        spec.program = QStringLiteral("cmd.exe");
    }
#else
    Q_UNUSED(spec);
#endif
}

} // namespace

QString sessionSettingsPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("claude-hooks.json"));
}

QByteArray sessionSettings(const QString &hookCommand, const QStringList &allowedTools)
{
    QJsonObject hooks;
    // The editor takes a snapshot before replying: Claude only starts once
    // the files are saved.
    hooks.insert(QLatin1String("UserPromptSubmit"), commandHook(hookCommand, {}, 30));
    // The editor refuses an edit that would overwrite unsaved changes.
    hooks.insert(QLatin1String("PreToolUse"),
                 commandHook(hookCommand, QStringLiteral("Edit|Write|MultiEdit|NotebookEdit"), 30));
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
    QJsonObject settings{{QLatin1String("hooks"), hooks}, {QLatin1String("statusLine"), statusLine}};
    if (!allowedTools.isEmpty())
        settings.insert(QLatin1String("permissions"),
                        QJsonObject{{QLatin1String("allow"), QJsonArray::fromStringList(allowedTools)}});
    return QJsonDocument(settings).toJson();
}

QString mcpServerPath()
{
    return companionPath(QStringLiteral("duet-mcp"));
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
        writeSettingsFile(sessionSettings(hook, mcpFile ? mcpReadOnlyTools.value_or(QStringList()) : QStringList()));
    if (!settingsFile) {
        *error = QCoreApplication::translate("ClaudeLauncher", "Could not write the hooks configuration.");
        return std::nullopt;
    }

    ProcessSpec spec;
    spec.program = claude;
    spec.arguments = QStringList{QStringLiteral("--settings"), *settingsFile};
    if (const QString plugin = pluginPath(); !plugin.isEmpty())
        spec.arguments << QStringLiteral("--plugin-dir") << QDir::toNativeSeparators(plugin);
    if (mcpFile)
        spec.arguments << QStringLiteral("--mcp-config") << *mcpFile;
    spec.arguments += options.extraArguments;
    spec.workingDirectory = workingDirectory;
    spec.environment = terminalEnvironment();
    spec.environment.insert(QStringLiteral("DUET_SOCKET"), hookServerName);
    // Our status line replaces the user's: duet-hook runs it and
    // displays its output.
    const QString userStatusLine = userStatusLineCommand(workingDirectory);
    if (!userStatusLine.isEmpty())
        spec.environment.insert(QStringLiteral("DUET_STATUSLINE_COMMAND"), userStatusLine);
    // If the editor was itself launched from Claude Code, this variable
    // would make `claude` believe it is nested in another session.
    spec.environment.remove(QStringLiteral("CLAUDECODE"));

    launchThroughCmd(spec);
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
    spec.environment.remove(QStringLiteral("CLAUDECODE"));
    launchThroughCmd(spec);
    return spec;
}

} // namespace ClaudeLauncher
