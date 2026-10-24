// SPDX-License-Identifier: GPL-3.0-only

#include "Shell.h"

#include <QProcess>
#include <QStandardPaths>

namespace Shell {
namespace {

QString usersShell()
{
    return qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
}

} // namespace

ShellCommand userShell()
{
    // A login shell: the PATH and the variables of the user's profile.
    return {.program = usersShell(), .arguments = {QStringLiteral("-l")}};
}

ShellCommand userShellRunning(const QString &command)
{
    return {.program = usersShell(), .arguments = {QStringLiteral("-c"), command}};
}

ShellCommand loginShellRunning(const QString &command)
{
    return {.program = usersShell(), .arguments = {QStringLiteral("-l"), QStringLiteral("-c"), command}};
}

ShellCommand systemShellRunning(const QString &command)
{
    return {.program = QStringLiteral("/bin/sh"), .arguments = {QStringLiteral("-c"), command}};
}

ShellCommand launchable(const QString &program, const QStringList &arguments)
{
    return {.program = program, .arguments = arguments};
}

QString programInWorkingFolder(const QString &name)
{
    return QStringLiteral("./") + name;
}

void prepareVerbatim(QProcess &process, const QString &command)
{
    const QString bash = QStandardPaths::findExecutable(QStringLiteral("bash"));
    process.setProgram(bash.isEmpty() ? QStringLiteral("/bin/sh") : bash);
    process.setArguments({QStringLiteral("-c"), command});
}

} // namespace Shell
