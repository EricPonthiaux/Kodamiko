// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The systems Duet runs on; Linux stands for the Unixes other than macOS.
enum class OperatingSystem {
    Windows,
    MacOS,
    Linux,
};

// The system Duet was built for, for the choices that only name it (the
// files of a download, a recipe's condition). What works differently on
// each system has an implementation per system instead: Shell,
// Executables, FilePaths…
namespace Host {

OperatingSystem operatingSystem();

} // namespace Host
