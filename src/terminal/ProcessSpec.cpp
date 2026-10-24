// SPDX-License-Identifier: GPL-3.0-only

#include "ProcessSpec.h"

#include "system/Shell.h"

#include <QDir>

namespace {

EnvironmentAdditions &terminalAdditions()
{
    static EnvironmentAdditions additions;
    return additions;
}

} // namespace

void addTo(QProcessEnvironment &environment, const EnvironmentAdditions &additions)
{
    for (auto it = additions.variables.cbegin(); it != additions.variables.cend(); ++it)
        environment.insert(it.key(), it.value());
    if (additions.pathFolders.isEmpty())
        return;
    QStringList folders;
    for (const QString &folder : additions.pathFolders)
        folders.append(QDir::toNativeSeparators(folder));
    const QString path = environment.value(QStringLiteral("PATH"));
    if (!path.isEmpty())
        folders.append(path);
    environment.insert(QStringLiteral("PATH"), folders.join(QDir::listSeparator()));
}

void setTerminalAdditions(const EnvironmentAdditions &additions)
{
    terminalAdditions() = additions;
}

QProcessEnvironment terminalEnvironment()
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    addTo(environment, terminalAdditions());
    environment.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    environment.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    return environment;
}

ProcessSpec defaultShell(const QString &workingDirectory)
{
    ProcessSpec spec;
    spec.workingDirectory = workingDirectory;
    spec.environment = terminalEnvironment();
    const ShellCommand shell = Shell::userShell();
    spec.program = shell.program;
    spec.arguments = shell.arguments;
    return spec;
}
