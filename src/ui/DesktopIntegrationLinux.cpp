// SPDX-License-Identifier: GPL-3.0-only

#include "DesktopIntegration.h"

// The desktop has no full-screen Spaces to follow.
void DesktopIntegration::followActiveSpace(QWidget * /*window*/)
{
}

// The menu of the taskbar's or the launcher's icon is the system's.
void DesktopIntegration::setDockMenu(QMenu * /*menu*/)
{
}

// No App Nap: a process in the background keeps its pace.
void DesktopIntegration::beginBackgroundActivity(const QString & /*reason*/)
{
}

void DesktopIntegration::endBackgroundActivity()
{
}
