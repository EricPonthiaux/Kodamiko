// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

// The systems Kodamiko runs on; Linux stands for the Unixes other than macOS.
enum class OperatingSystem {
    Windows,
    MacOS,
    Linux,
};

// The system Kodamiko was built for, for the choices that only name it (the
// files of a download, a recipe's condition). What works differently on
// each system has an implementation per system instead: Shell,
// Executables, FilePaths…
namespace Host {

OperatingSystem operatingSystem();
// "windows", "macos" or "linux": the system's name in recipes and
// run.json.
QString systemName();

} // namespace Host
