// SPDX-License-Identifier: GPL-3.0-only

#include "projects/WindowForeground.h"

#include <windows.h>

// Any process: the one that answers for a project is not known by its id
// here, and the right lasts until the next input or activation.
void WindowForeground::allowOtherProcess()
{
    AllowSetForegroundWindow(ASFW_ANY);
}
