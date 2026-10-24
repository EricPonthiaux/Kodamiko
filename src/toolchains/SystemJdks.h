// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QStringList>

// Where the system's installers and package managers put JDKs, one JDK per
// subfolder. One implementation per system: SystemJdksWindows.cpp,
// SystemJdksMacOS.cpp, SystemJdksLinux.cpp.
namespace SystemJdks {

QStringList parentFolders();

} // namespace SystemJdks
