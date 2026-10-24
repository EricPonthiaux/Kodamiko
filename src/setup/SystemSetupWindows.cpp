// SPDX-License-Identifier: GPL-3.0-only

#include "SystemSetup.h"

#include <QCoreApplication>

namespace SystemSetup {

QString gitPurpose()
{
    return QCoreApplication::translate("Prerequisites",
                                       "Staging, commits, discarding changes and snapshots. Claude Code also needs "
                                       "Git for Windows (Git Bash) to run commands.");
}

QString gitInstallCommand()
{
    return QStringLiteral("winget install --id Git.Git -e --source winget");
}

QString claudeInstallCommand()
{
    return QStringLiteral("irm https://claude.ai/install.ps1 | iex");
}

QString programCall(const QString &path, const QString &arguments)
{
    // "&": PowerShell calls the quoted path instead of printing a string.
    return QStringLiteral("& \"%1\" %2").arg(path, arguments);
}

ShellCommand installerShell(const QString &command)
{
    // The installers are PowerShell scripts, which the default execution
    // policy refuses.
    return {.program = QStringLiteral("powershell.exe"),
            .arguments = {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                          QStringLiteral("-Command"), command}};
}

} // namespace SystemSetup
