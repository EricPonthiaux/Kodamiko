// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>
#include <QStringList>

class QProcess;

// A program and its arguments, as QProcess and the terminals start them.
struct ShellCommand {
    QString program;
    QStringList arguments;
};

// The system's shells, and how a command line reaches them. One
// implementation per system: ShellWindows.cpp (PowerShell, cmd.exe),
// ShellUnix.cpp (the user's $SHELL, /bin/sh).
namespace Shell {

// The user's interactive shell, for a terminal tab: PowerShell 7 (pwsh),
// else Windows PowerShell; a login $SHELL elsewhere.
ShellCommand userShell();

// `command` run by the user's shell, which then exits.
ShellCommand userShellRunning(const QString &command);

// `command` run with the PATH of the user's profile: by cmd.exe on
// Windows, by a login $SHELL elsewhere.
ShellCommand loginShellRunning(const QString &command);

// `command` run by the system's plain shell: cmd.exe, /bin/sh.
ShellCommand systemShellRunning(const QString &command);

// `program` with `arguments` as the system can start it: CreateProcess
// starts a batch file (.cmd, .bat, as npm installs) only through cmd.exe;
// elsewhere unchanged.
ShellCommand launchable(const QString &program, const QStringList &arguments);

// The shell's name of the program `name` of its working folder: "mvnw" on
// Windows, "./mvnw" elsewhere.
QString programInWorkingFolder(const QString &name);

// Sets `process` up to run `command` exactly as written, as Claude Code
// runs the commands of its settings: cmd.exe without Qt's quoting on
// Windows; bash, or /bin/sh without it, elsewhere.
void prepareVerbatim(QProcess &process, const QString &command);

} // namespace Shell
