// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QMap>
#include <QString>

#include <optional>

// The environment of Visual Studio's x64 C++ tools, which Qt's MSVC kits
// build with. Visual Studio exists on Windows only: VisualStudio.cpp is
// compiled in the Windows build alone.
namespace VisualStudio {

// The variables `vcvars64.bat` sets (INCLUDE, LIB, the PATH with cl, the
// SDK, Visual Studio's CMake and Ninja...), as an x64 Native Tools prompt
// has them; nullopt, with `error` set, on a machine without Visual Studio's
// C++ tools. Running vcvars64 takes about two seconds: the first result is
// kept for the rest of the process.
std::optional<QMap<QString, QString>> environment(QString *error);

} // namespace VisualStudio
