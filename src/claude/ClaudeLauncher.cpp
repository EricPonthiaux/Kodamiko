#include "ClaudeLauncher.h"

#include <QCoreApplication>
#include <QDir>
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
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("enlumine-hook.exe");
#else
    const QString name = QStringLiteral("enlumine-hook");
#endif
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(name);
    return QFileInfo::exists(path) ? path : QString();
}

QJsonArray commandHook(const QString &command, const QString &matcher = {})
{
    QJsonObject entry;
    if (!matcher.isEmpty())
        entry.insert(QLatin1String("matcher"), matcher);
    entry.insert(QLatin1String("hooks"), QJsonArray{QJsonObject{
                                             {QLatin1String("type"), QLatin1String("command")},
                                             {QLatin1String("command"), command},
                                             {QLatin1String("timeout"), 60},
                                         }});
    return QJsonArray{entry};
}

std::optional<QString> writeSettingsFile(const QByteArray &settings)
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(directory);
    const QString path = QDir(directory).filePath(QStringLiteral("claude-hooks.json"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(settings) != settings.size() || !file.commit())
        return std::nullopt;
    return path;
}

} // namespace

QByteArray hooksSettings(const QString &hookCommand)
{
    QJsonObject hooks;
    hooks.insert(QLatin1String("PostToolUse"), commandHook(hookCommand, QStringLiteral("Edit|Write|MultiEdit|NotebookEdit")));
    hooks.insert(QLatin1String("Stop"), commandHook(hookCommand));
    return QJsonDocument(QJsonObject{{QLatin1String("hooks"), hooks}}).toJson();
}

QString findClaudeExecutable()
{
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("claude"));
    if (!inPath.isEmpty())
        return inPath;

    // Lancé depuis le bureau, l'éditeur n'hérite pas toujours du PATH du
    // shell : on essaie les emplacements d'installation habituels.
    const QString home = QDir::homePath();
    return QStandardPaths::findExecutable(QStringLiteral("claude"),
                                          {home + QStringLiteral("/.local/bin"),
                                           home + QStringLiteral("/.claude/local"),
                                           home + QStringLiteral("/.npm-global/bin")});
}

std::optional<ProcessSpec> claudeProcess(const QString &workingDirectory, const QString &hookServerName,
                                         QString *error)
{
    const QString claude = findClaudeExecutable();
    if (claude.isEmpty()) {
        *error = QCoreApplication::translate("ClaudeLauncher",
                                             "La commande « claude » est introuvable. Installez Claude Code "
                                             "(https://claude.com/claude-code) puis relancez l'éditeur.");
        return std::nullopt;
    }
    const QString hook = hookExecutablePath();
    if (hook.isEmpty()) {
        *error = QCoreApplication::translate("ClaudeLauncher",
                                             "Le programme enlumine-hook est absent du dossier de l'éditeur.");
        return std::nullopt;
    }

    // Barres obliques et guillemets : la commande est valide pour sh comme
    // pour cmd.exe, même si le chemin contient des espaces.
    const QString hookCommand = QLatin1Char('"') + QDir::fromNativeSeparators(hook) + QLatin1Char('"');
    const std::optional<QString> settingsFile = writeSettingsFile(hooksSettings(hookCommand));
    if (!settingsFile) {
        *error = QCoreApplication::translate("ClaudeLauncher", "Impossible d'écrire la configuration des hooks.");
        return std::nullopt;
    }

    ProcessSpec spec;
    spec.program = claude;
    spec.arguments = {QStringLiteral("--settings"), *settingsFile};
    spec.workingDirectory = workingDirectory;
    spec.environment = terminalEnvironment();
    spec.environment.insert(QStringLiteral("ENLUMINE_SOCKET"), hookServerName);
    // Si l'éditeur a lui-même été lancé depuis Claude Code, cette variable
    // ferait croire à `claude` qu'il est imbriqué dans une autre session.
    spec.environment.remove(QStringLiteral("CLAUDECODE"));

#ifdef Q_OS_WIN
    // Une installation npm fournit claude.cmd, que CreateProcess ne lance
    // qu'à travers cmd.exe.
    if (claude.endsWith(QLatin1String(".cmd"), Qt::CaseInsensitive)
        || claude.endsWith(QLatin1String(".bat"), Qt::CaseInsensitive)) {
        spec.arguments.prepend(QDir::toNativeSeparators(claude));
        spec.arguments.prepend(QStringLiteral("/c"));
        spec.program = QStringLiteral("cmd.exe");
    }
#endif
    return spec;
}

} // namespace ClaudeLauncher
