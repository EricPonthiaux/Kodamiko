// SPDX-License-Identifier: GPL-3.0-only

#include "app/NativeNotifications.h"

#include <QCoreApplication>
#include <QMetaObject>

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

#include <utility>

namespace {

enum class Permission {
    Unknown,
    Granted,
    Refused,
};

// The user's answer, asked on the first notification. Touched in the main
// thread only.
Permission permission = Permission::Unknown;
// What a click on Duet's notification does: the last window that showed
// one (one window per process). Main thread only.
std::function<void()> clickedHandler;

void runInMainThread(std::function<void()> function)
{
    QMetaObject::invokeMethod(qApp, std::move(function), Qt::QueuedConnection);
}

} // namespace

// UNUserNotificationCenter calls its delegate in a thread of its own.
@interface DuetNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

@implementation DuetNotificationDelegate

// Shown even while Duet is the active application: another of its windows
// may be in front.
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler
{
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionSound);
}

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
    didReceiveNotificationResponse:(UNNotificationResponse *)response
             withCompletionHandler:(void (^)(void))completionHandler
{
    runInMainThread([] {
        if (clickedHandler)
            clickedHandler();
    });
    completionHandler();
}

@end

namespace {

// Nil outside a bundle (a build run as a bare executable, the tests):
// UNUserNotificationCenter raises an exception there.
UNUserNotificationCenter *notificationCenter()
{
    if (!NSBundle.mainBundle.bundleIdentifier)
        return nil;
    UNUserNotificationCenter *center = UNUserNotificationCenter.currentNotificationCenter;
    if (!center.delegate)
        center.delegate = [[DuetNotificationDelegate alloc] init]; // kept for the process's life
    return center;
}

void post(UNUserNotificationCenter *center, const QString &title, const QString &message)
{
    UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
    content.title = title.toNSString();
    content.body = message.toNSString();
    content.sound = UNNotificationSound.defaultSound;
    UNNotificationRequest *request = [UNNotificationRequest requestWithIdentifier:NSUUID.UUID.UUIDString
                                                                          content:content
                                                                          trigger:nil];
    [content release]; // the request retains it
    [center addNotificationRequest:request withCompletionHandler:nil];
}

} // namespace

bool NativeNotifications::show(const QString &title, const QString &message, std::function<void()> clicked)
{
    UNUserNotificationCenter *center = notificationCenter();
    if (!center || permission == Permission::Refused)
        return false;
    clickedHandler = std::move(clicked);
    if (permission == Permission::Granted) {
        post(center, title, message);
        return true;
    }
    // The first notification waits for the user's answer. Refused, the
    // next ones go through the tray.
    // A block captures a C++ reference as a reference: copies, so that the
    // answer, which comes later in another thread, does not read the
    // caller's strings once they are gone.
    const QString titleCopy = title;
    const QString messageCopy = message;
    const UNAuthorizationOptions options = UNAuthorizationOptionAlert | UNAuthorizationOptionSound;
    [center requestAuthorizationWithOptions:options
                          completionHandler:^(BOOL granted, NSError *) {
                            runInMainThread([granted, center, titleCopy, messageCopy] {
                                permission = granted ? Permission::Granted : Permission::Refused;
                                if (granted)
                                    post(center, titleCopy, messageCopy);
                            });
                          }];
    return true;
}

void NativeNotifications::clear()
{
    [notificationCenter() removeAllDeliveredNotifications];
}
