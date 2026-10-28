// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>
#include <QStringList>

// The names of executables, which depend on the system. One
// implementation per system: ExecutablesWindows.cpp, ExecutablesUnix.cpp;
// what they share in Executables.cpp.
namespace Executables {

// The file of the executable `path` names: "bin/clangd" is
// "bin/clangd.exe" on Windows.
QString fileName(const QString &path);

// The program `name` shipped next to the running one (kodamiko-hook beside
// kodamiko), or an empty string.
QString besideTheApplication(const QString &name);

// The Python of the virtual environment `folder` (a .venv):
// Scripts/python.exe on Windows, bin/python elsewhere.
QString virtualEnvironmentPython(const QString &folder);

// The names Python goes by in the PATH, the most usual first.
QStringList pythonNames();

} // namespace Executables
