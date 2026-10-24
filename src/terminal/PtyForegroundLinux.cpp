// SPDX-License-Identifier: GPL-3.0-only

#include "PtyForeground.h"

#include <unistd.h>

namespace PtyForeground {

pid_t group(int masterFd, [[maybe_unused]] pid_t sessionLeader)
{
    return ::tcgetpgrp(masterFd);
}

} // namespace PtyForeground
