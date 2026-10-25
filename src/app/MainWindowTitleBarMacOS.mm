// SPDX-License-Identifier: GPL-3.0-only

#include "MainWindow.h"

#include "ui/AppFonts.h"
#include "ui/TitleBar.h"
#include "ui/UiMetrics.h"

#import <AppKit/AppKit.h>

#include <cmath>
#include <cstdlib>

namespace {

// Where macOS centers the traffic lights, from the top of the window, with
// a compact and a regular toolbar (measured on macOS 26).
constexpr int compactToolbarButtonsCenterPx = 20;
constexpr int toolbarButtonsCenterPx = 26;
// The Duet mark beside the traffic lights, a little smaller than the bar's
// buttons.
constexpr qreal logoScale = 0.8;

NSWindow *nativeWindow(QWidget *window)
{
    // A widget's winId is its NSView on macOS.
    return reinterpret_cast<NSView *>(window->winId()).window;
}

// Makes the system title bar the height, among those macOS offers, that
// centers the traffic lights closest to the middle of a bar `barHeight`
// pixels high. Clicks under the title bar still go to the window's widgets.
void fitTitleBarHeight(QWidget *window, int barHeight)
{
    NSWindow *nsWindow = nativeWindow(window);
    // An empty toolbar is the only way AppKit offers to make the title bar
    // taller and lower the traffic lights.
    if (!nsWindow.toolbar) {
        NSToolbar *toolbar = [[NSToolbar alloc] initWithIdentifier:@"Duet"];
        nsWindow.toolbar = toolbar;
        [toolbar release]; // the window retains it
    }
    // In full screen, macOS draws the toolbar opaque, over Duet's bar.
    nsWindow.toolbar.visible = !window->isFullScreen();
    const int barCenter = barHeight / 2;
    const bool isCompactCloser =
        std::abs(barCenter - compactToolbarButtonsCenterPx) <= std::abs(barCenter - toolbarButtonsCenterPx);
    nsWindow.toolbarStyle = isCompactCloser ? NSWindowToolbarStyleUnifiedCompact : NSWindowToolbarStyleUnified;
}

NSRect inWindow(NSButton *button)
{
    return [button convertRect:button.bounds toView:nil];
}

// The width, from the left edge of `window`, that the traffic lights cover.
int trafficLightsWidth(QWidget *window)
{
    NSButton *zoom = [nativeWindow(window) standardWindowButton:NSWindowZoomButton];
    return int(std::ceil(NSMaxX(inWindow(zoom))));
}

// The gap between the left edge of `window` and its traffic lights.
int trafficLightsMargin(QWidget *window)
{
    NSButton *close = [nativeWindow(window) standardWindowButton:NSWindowCloseButton];
    return int(std::floor(NSMinX(inWindow(close))));
}

} // namespace

// macOS keeps its frame: `system` changes nothing. Duet's bar lies under the
// transparent system title bar, the traffic lights at its left.
void MainWindow::setSystemTitleBar(bool system)
{
    Q_UNUSED(system);
    titleBar_->setWindowControlsVisible(false);
    titleBar_->setLogoScale(logoScale);
    setWindowFlag(Qt::ExpandedClientAreaHint, true);
    setWindowFlag(Qt::NoTitleBarBackgroundHint, true);
    // Qt would push the whole window below the system title bar, leaving
    // Duet's bar under an empty strip instead of beside the traffic lights.
    setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
    // After the flags, which may recreate the native window: its title
    // would cover Duet's logo and menu.
    nativeWindow(this).titleVisibility = NSWindowTitleHidden;
    // The bar's height follows the font: the traffic lights, its middle.
    AppFonts::followApplicationFont(this, [this] { fitTitleBarToWindowState(); });
}

// In full screen, macOS hides the traffic lights: their room goes too. The
// buttons at the right keep the traffic lights' margin from the edge.
void MainWindow::fitTitleBarToWindowState()
{
    fitTitleBarHeight(this, UiMetrics::scaled(UiMetrics::titleBarHeight));
    titleBar_->setLeadingSpace(isFullScreen() ? 0 : trafficLightsWidth(this));
    titleBar_->setTrailingSpace(trafficLightsMargin(this));
}
