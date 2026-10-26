// SPDX-License-Identifier: GPL-3.0-only

#include "terminal/PtyForeground.h"

#include <sys/sysctl.h>

namespace PtyForeground {

pid_t group([[maybe_unused]] int masterFd, pid_t sessionLeader)
{
    // Darwin answers TIOCGPGRP (tcgetpgrp) only to a process the terminal
    // controls; the process table gives the group to any process of the user.
    kinfo_proc info{};
    size_t size = sizeof info;
    int query[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, sessionLeader};
    if (::sysctl(query, 4, &info, &size, nullptr, 0) != 0 || size == 0)
        return -1;
    return info.kp_eproc.e_tpgid;
}

} // namespace PtyForeground
