// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>
#include <QMap>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <optional>

// The environment of Visual Studio's MSVC on Windows (Qt kits: see
// Toolchains::qtFolder).
namespace DeveloperEnvironment {

// The variables `vcvars64.bat` sets (INCLUDE, LIB, the PATH with cl, the
// SDK, Visual Studio's CMake and Ninja...), as an x64 Native Tools prompt
// has them; nullopt, with `error` set, on a machine without Visual Studio's
// C++ tools or outside Windows. Running vcvars64 takes about two seconds:
// the first result is kept for the rest of the process.
std::optional<QMap<QString, QString>> visualStudio(QString *error);

// The variables of `setOutput` (cmd's `set`, one NAME=value per line) that
// `before` lacks or has with another value.
QMap<QString, QString> changedVariables(const QByteArray &setOutput, const QProcessEnvironment &before);

} // namespace DeveloperEnvironment
