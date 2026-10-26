// SPDX-License-Identifier: GPL-3.0-only

#include "DesktopIntegration.h"

#include <QGuiApplication>
#include <QWidget>

#import <AppKit/AppKit.h>

namespace {

// Nil without the Cocoa platform (offscreen tests): messages to nil do
// nothing.
NSWindow *nativeWindow(QWidget *window)
{
    if (QGuiApplication::platformName() != QLatin1String("cocoa"))
        return nil;
    // A widget's winId is its NSView with the Cocoa platform.
    return reinterpret_cast<NSView *>(window->winId()).window;
}

} // namespace

// An auxiliary window may share a full-screen window's Space; moving to
// the active Space brings it there instead of switching Spaces. The level
// stays normal: the window does not float above the others.
void DesktopIntegration::followActiveSpace(QWidget *window)
{
    NSWindow *nsWindow = nativeWindow(window);
    nsWindow.collectionBehavior = (nsWindow.collectionBehavior & ~NSWindowCollectionBehaviorFullScreenPrimary)
                                  | NSWindowCollectionBehaviorFullScreenAuxiliary
                                  | NSWindowCollectionBehaviorMoveToActiveSpace;
}
