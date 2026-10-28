// SPDX-License-Identifier: GPL-3.0-only

#include "ui/DesktopIntegration.h"

#include <QMenuBar>

#include <shobjidl_core.h>

void DesktopIntegration::identifyApplication()
{
    // A failure only leaves the system's default identity, derived from
    // the executable's path.
    SetCurrentProcessExplicitAppUserModelID(L"Kodamiko.Kodamiko");
}

// The desktop has no full-screen Spaces to follow.
void DesktopIntegration::followActiveSpace(QWidget * /*window*/)
{
}

// The menu of the taskbar's or the launcher's icon is the system's.
void DesktopIntegration::setDockMenu(QMenu * /*menu*/)
{
}

// The window's own menu bar holds every command.
std::unique_ptr<QMenuBar> DesktopIntegration::createNativeMenuBar(const QList<QAction *> & /*actions*/)
{
    return nullptr;
}

// No App Nap: a process in the background keeps its pace.
void DesktopIntegration::beginBackgroundActivity(const QString & /*reason*/)
{
}

void DesktopIntegration::endBackgroundActivity()
{
}
