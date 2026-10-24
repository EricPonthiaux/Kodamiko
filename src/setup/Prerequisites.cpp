// SPDX-License-Identifier: GPL-3.0-only

#include "Prerequisites.h"

#include "SystemSetup.h"
#include "agent/vendor/claude/ClaudeLauncher.h"

#include <QCoreApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace Prerequisites {
namespace {

constexpr int versionTimeoutMs = 10000;

QString tr(const char *text)
{
    return QCoreApplication::translate("Prerequisites", text);
}

QString findGit()
{
    return QStandardPaths::findExecutable(QStringLiteral("git"));
}

QVersionNumber programVersion(const QString &program)
{
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("CLAUDECODE"));
    process.setProcessEnvironment(environment);
    process.start(program, {QStringLiteral("--version")});
    if (!process.waitForFinished(versionTimeoutMs)) {
        process.kill();
        process.waitForFinished();
        return {};
    }
    return parseVersion(QString::fromUtf8(process.readAllStandardOutput()));
}

} // namespace

bool allFound()
{
    return !findGit().isEmpty() && !ClaudeLauncher::findClaudeExecutable().isEmpty();
}

QList<Prerequisite> check()
{
    Prerequisite git;
    git.id = Prerequisite::Id::Git;
    git.name = QStringLiteral("Git");
    git.purpose = SystemSetup::gitPurpose();
    git.minimumVersion = QVersionNumber(2, 23); // git restore
    git.path = findGit();
    git.installCommand = SystemSetup::gitInstallCommand();
    git.helpUrl = QUrl(QStringLiteral("https://git-scm.com/downloads"));

    Prerequisite claude;
    claude.id = Prerequisite::Id::ClaudeCode;
    claude.name = QStringLiteral("Claude Code");
    claude.purpose = tr("The Claude session. On its first launch, it asks you to log in to your Claude account.");
    // Hooks and status line data the editor relies on.
    claude.minimumVersion = QVersionNumber(2, 1);
    claude.path = ClaudeLauncher::findClaudeExecutable();
    claude.helpUrl = QUrl(QStringLiteral("https://code.claude.com/docs/en/setup"));

    QList<Prerequisite> prerequisites{git, claude};
    for (Prerequisite &prerequisite : prerequisites) {
        if (prerequisite.path.isEmpty())
            continue;
        prerequisite.version = programVersion(prerequisite.path);
        // An unreadable version does not prevent using the program.
        prerequisite.status = !prerequisite.version.isNull() && prerequisite.version < prerequisite.minimumVersion
                                  ? Prerequisite::Status::Outdated
                                  : Prerequisite::Status::Ok;
    }

    Prerequisite &claudeResult = prerequisites[1];
    if (claudeResult.status == Prerequisite::Status::Outdated) {
        claudeResult.installCommand = QStringLiteral("claude update");
    } else {
        claudeResult.installCommand = SystemSetup::claudeInstallCommand();
    }
    return prerequisites;
}

QVersionNumber parseVersion(const QString &output)
{
    static const QRegularExpression number(QStringLiteral("(\\d+)\\.(\\d+)(?:\\.(\\d+))?"));
    return QVersionNumber::fromString(number.match(output).captured(0));
}

QString linuxInstallCommand(const QString &osId, const QString &osIdLike, const QString &package)
{
    const QStringList ids = (osId + QLatin1Char(' ') + osIdLike).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const auto is = [&ids](std::initializer_list<const char *> names) {
        return std::ranges::any_of(names, [&ids](const char *name) { return ids.contains(QLatin1String(name)); });
    };
    // Debian-like before Fedora-like: some distributions list both.
    if (is({"debian", "ubuntu"}))
        return QStringLiteral("sudo apt install -y %1").arg(package);
    if (is({"fedora", "rhel", "centos"}))
        return QStringLiteral("sudo dnf install -y %1").arg(package);
    if (is({"arch"}))
        return QStringLiteral("sudo pacman -S --needed %1").arg(package);
    if (is({"opensuse", "suse", "sles"}))
        return QStringLiteral("sudo zypper install -y %1").arg(package);
    if (is({"alpine"}))
        return QStringLiteral("sudo apk add %1").arg(package);
    return {};
}

ProcessSpec installProcess(const Prerequisite &prerequisite, const QString &workingDirectory)
{
    ProcessSpec spec;
    spec.workingDirectory = workingDirectory;
    spec.environment = terminalEnvironment();
    spec.environment.remove(QStringLiteral("CLAUDECODE"));
    QString command = prerequisite.installCommand;
    // "claude update" with the program found, which may not be in the PATH.
    if (command == QLatin1String("claude update"))
        command = SystemSetup::programCall(prerequisite.path, QStringLiteral("update"));
    const ShellCommand shell = SystemSetup::installerShell(command);
    spec.program = shell.program;
    spec.arguments = shell.arguments;
    return spec;
}

} // namespace Prerequisites
