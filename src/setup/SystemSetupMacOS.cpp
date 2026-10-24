// SPDX-License-Identifier: GPL-3.0-only

#include "SystemSetup.h"

#include <QCoreApplication>

namespace SystemSetup {

QString gitPurpose()
{
    return QCoreApplication::translate("Prerequisites", "Staging, commits, discarding changes and snapshots.");
}

QString gitInstallCommand()
{
    // Git comes with Xcode's command line tools.
    return QStringLiteral("xcode-select --install");
}

QString claudeInstallCommand()
{
    return QStringLiteral("curl -fsSL https://claude.ai/install.sh | bash");
}

QString programCall(const QString &path, const QString &arguments)
{
    return QStringLiteral("\"%1\" %2").arg(path, arguments);
}

ShellCommand installerShell(const QString &command)
{
    return Shell::systemShellRunning(command);
}

} // namespace SystemSetup
