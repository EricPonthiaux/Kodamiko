// SPDX-License-Identifier: GPL-3.0-only

#include "DesktopIntegration.h"

#include <QGuiApplication>
#include <QMenu>
#include <QMenuBar>
#include <QString>
#include <QWidget>

#import <AppKit/AppKit.h>

namespace {

// The NSProcessInfo activity while one is going on, retained; nil
// otherwise.
id<NSObject> backgroundActivity = nil;

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

void DesktopIntegration::setDockMenu(QMenu *menu)
{
    menu->setAsDockMenu();
}

// Without a parent, a QMenuBar is the native one, shared by the windows
// of the process.
std::unique_ptr<QMenuBar> DesktopIntegration::createNativeMenuBar(const QList<QAction *> &actions)
{
    auto bar = std::make_unique<QMenuBar>();
    QMenu *menu = bar->addMenu(QStringLiteral("Duet"));
    menu->addActions(actions);
    return bar;
}

// The system may still sleep when idle: only Duet's own work is kept going.
void DesktopIntegration::beginBackgroundActivity(const QString &reason)
{
    if (backgroundActivity)
        return;
    backgroundActivity =
        [[NSProcessInfo.processInfo beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep
                                                      reason:reason.toNSString()] retain];
}

void DesktopIntegration::endBackgroundActivity()
{
    if (!backgroundActivity)
        return;
    [NSProcessInfo.processInfo endActivity:backgroundActivity];
    [backgroundActivity release];
    backgroundActivity = nil;
}
