// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <sys/types.h>

// Which process group holds a Unix pseudo-terminal. One implementation per
// system: PtyForegroundMacOS.cpp, PtyForegroundLinux.cpp (and the other
// Unixes).
namespace PtyForeground {

// The process group that holds the terminal of `sessionLeader`, whose
// master side is `masterFd`; -1 if unknown.
pid_t group(int masterFd, pid_t sessionLeader);

} // namespace PtyForeground
