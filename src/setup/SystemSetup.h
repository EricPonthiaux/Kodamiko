// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "system/Shell.h"

#include <QString>

// What installing and updating the prerequisites takes on each system. One
// implementation per system: SystemSetupWindows.cpp (winget, PowerShell),
// SystemSetupMacOS.cpp (Xcode's command line tools), SystemSetupLinux.cpp
// (the distribution's package manager).
namespace SystemSetup {

// What Git is for, as the prerequisites list says it.
QString gitPurpose();

// The command that installs Git; empty when the system's way is unknown.
QString gitInstallCommand();

// The command that installs Claude Code.
QString claudeInstallCommand();

// The command that runs the program `path` with `arguments`, in the syntax
// of the installer's shell.
QString programCall(const QString &path, const QString &arguments);

// The installer's shell running `command`.
ShellCommand installerShell(const QString &command);

} // namespace SystemSetup
