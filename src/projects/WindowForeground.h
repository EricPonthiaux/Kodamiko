// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Handing the foreground to another Kodamiko process, so that the window it is
// asked to show comes forward instead of flashing in the taskbar. One
// source per system.
namespace WindowForeground {

// Lets the next process that activates a window take the foreground from
// this one: Windows refuses SetForegroundWindow to a process in the
// background unless the process in the foreground allows it. Call it
// before asking another process to show its window. Does nothing where
// the system does not restrict the foreground.
void allowOtherProcess();

} // namespace WindowForeground
