// SPDX-License-Identifier: GPL-3.0-only

#include "Shell.h"

#include <QDir>
#include <QProcess>
#include <QStandardPaths>

namespace Shell {
namespace {

QString commandPrompt()
{
    return qEnvironmentVariable("ComSpec", QStringLiteral("cmd.exe"));
}

QString powerShell()
{
    const QString pwsh = QStandardPaths::findExecutable(QStringLiteral("pwsh"));
    return pwsh.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("powershell")) : pwsh;
}

bool isBatchFile(const QString &program)
{
    return program.endsWith(QLatin1String(".cmd"), Qt::CaseInsensitive) ||
           program.endsWith(QLatin1String(".bat"), Qt::CaseInsensitive);
}

} // namespace

ShellCommand userShell()
{
    return {.program = powerShell(), .arguments = {QStringLiteral("-NoLogo")}};
}

ShellCommand userShellRunning(const QString &command)
{
    return {.program = powerShell(), .arguments = {QStringLiteral("-NoLogo"), QStringLiteral("-Command"), command}};
}

ShellCommand loginShellRunning(const QString &command)
{
    // /d: without the AutoRun commands of the registry; /s: the command
    // line as written, outer quotes included.
    return {.program = commandPrompt(),
            .arguments = {QStringLiteral("/d"), QStringLiteral("/s"), QStringLiteral("/c"), command}};
}

ShellCommand systemShellRunning(const QString &command)
{
    return {.program = QStringLiteral("cmd.exe"), .arguments = {QStringLiteral("/c"), command}};
}

ShellCommand launchable(const QString &program, const QStringList &arguments)
{
    if (!isBatchFile(program))
        return {.program = program, .arguments = arguments};
    return {.program = QStringLiteral("cmd.exe"),
            .arguments = QStringList{QStringLiteral("/c"), QDir::toNativeSeparators(program)} + arguments};
}

QString programInWorkingFolder(const QString &name)
{
    return name;
}

void prepareVerbatim(QProcess &process, const QString &command)
{
    process.setProgram(QStringLiteral("cmd.exe"));
    process.setNativeArguments(QStringLiteral("/c ") + command);
}

} // namespace Shell
