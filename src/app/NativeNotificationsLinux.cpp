// SPDX-License-Identifier: GPL-3.0-only

#include "NativeNotifications.h"

// The system tray shows Duet's notifications here.
bool NativeNotifications::show(const QString & /*title*/,
                               const QString & /*message*/,
                               std::function<void()> /*clicked*/)
{
    return false;
}

void NativeNotifications::clear()
{
}
