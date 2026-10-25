// SPDX-License-Identifier: GPL-3.0-only

#include "MainWindow.h"

#include "ui/DetachablePage.h"
#include "ui/TitleBar.h"
#include "ui/WindowFrame.h"

#include <QStatusBar>

void MainWindow::setSystemTitleBar(bool system)
{
    frame_->setFrameless(!system);
    terminalWindow_->setFrameless(!system);
    titleBar_->setWindowControlsVisible(!system);
    statusBar()->setSizeGripEnabled(system); // Duet's frame resizes from its edges
}

void MainWindow::fitTitleBarToWindowState()
{
    // Duet's window controls, or the system's frame: nothing to move.
}
