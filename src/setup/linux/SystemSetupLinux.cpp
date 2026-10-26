// SPDX-License-Identifier: GPL-3.0-only

#include "setup/Prerequisites.h"
#include "setup/SystemSetup.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>

namespace SystemSetup {
namespace {

QString osReleaseField(const QByteArray &osRelease, const QString &key)
{
    const QRegularExpression field(QStringLiteral("^%1=\"?([^\"\\n]*)\"?$").arg(key),
                                   QRegularExpression::MultilineOption);
    return field.match(QString::fromUtf8(osRelease)).captured(1);
}

} // namespace

QString gitPurpose()
{
    return QCoreApplication::translate("Prerequisites", "Staging, commits, discarding changes and snapshots.");
}

QString gitInstallCommand()
{
    // The distribution's package manager, which /etc/os-release names.
    QFile file(QStringLiteral("/etc/os-release"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray osRelease = file.readAll();
    return Prerequisites::linuxInstallCommand(osReleaseField(osRelease, QStringLiteral("ID")),
                                              osReleaseField(osRelease, QStringLiteral("ID_LIKE")),
                                              QStringLiteral("git"));
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
