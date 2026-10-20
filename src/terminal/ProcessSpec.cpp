// SPDX-License-Identifier: GPL-3.0-only

#include "ProcessSpec.h"

#include <QStandardPaths>

QProcessEnvironment terminalEnvironment()
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    environment.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    return environment;
}

ProcessSpec defaultShell(const QString &workingDirectory)
{
    ProcessSpec spec;
    spec.workingDirectory = workingDirectory;
    spec.environment = terminalEnvironment();
#ifdef Q_OS_WIN
    spec.program = QStandardPaths::findExecutable(QStringLiteral("pwsh"));
    if (spec.program.isEmpty())
        spec.program = QStandardPaths::findExecutable(QStringLiteral("powershell"));
    spec.arguments = {QStringLiteral("-NoLogo")};
#else
    spec.program = qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
    spec.arguments = {QStringLiteral("-l")};
#endif
    return spec;
}
