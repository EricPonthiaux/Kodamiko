# macOS integration

What Duet needs to fit in a macOS environment: Spaces and full screen,
bundle identity, signing, notifications, Dock, keyboard. Only the fixes that
apply to Duet's architecture (Qt 6 Widgets, one process per project window,
Duet's own menu bar in the title bar) are kept.

Sources: Duet's code, the built bundle (`build/bin/Duet.app`), Qt 6.10's
Cocoa plugin (`qcocoawindow.mm`, `qcocoasystemtrayicon.mm`) and the
references at the end.

---

## 1. Spaces and full screen

### How Qt places a window in the Spaces

`QCocoaWindow` (Qt 6.10) makes each top-level window an `NSWindow` or an
`NSPanel`:

| Qt window type | Native window | Spaces |
|---|---|---|
| `Qt::Window` without parent | `NSWindow`, `FullScreenPrimary` | Never joins another window's full-screen Space: macOS switches to the desktop to show it. |
| `Qt::Dialog`, `Qt::Popup`, `Qt::Tool` | `NSPanel`, `FullScreenAuxiliary \| MoveToActiveSpace` | Opens on the current Space, full screen included. |
| Child widget (`OverlayPopup`) | none | Always safe. |

Qt already sets `tabbingMode = Disallowed` and `restorable = NO`: native
window tabbing and state restoration need no work.

Qt resets `collectionBehavior` in `setWindowFlags` and whenever it recreates
the native window: a value set through AppKit must be set again after each
`setWindowFlag`.

### 1.1 Widgets shown before they have a parent

A widget shown without a parent is a `Qt::Window`, even for a moment: from a
full-screen window, macOS leaves the Space. Commit `a942afd` fixed one case
(the font sources' label in Preferences); nothing prevents the next.

**Fix:** a debug-only event filter on `qApp` that warns (`qWarning`) when a
top-level `Qt::Window` is shown that is not a `MainWindow`, the
`WelcomeWindow` or the detached terminal.

### 1.2 The detached terminal window

`DetachablePage::detach` (`src/ui/DetachablePage.cpp`) creates a parentless
`QWidget`, deliberately: a window the main one does not cover. From a
full-screen main window, it opens on the desktop.

Options:

1. **Recommended:** in an `.mm`, after `winId()` and after any
   `setWindowFlag`, set its `collectionBehavior` to
   `FullScreenAuxiliary | MoveToActiveSpace`, at the normal window level: it
   follows the main window into its Space.
2. `Qt::Tool` with the main window as parent and
   `Qt::WA_MacAlwaysShowToolWindow`: simpler, but it floats above every
   window (`NSFloatingWindowLevel`).
3. Keep it and document that it goes beside the main window in full-screen
   Split View, which already works.

### 1.3 Reused dialogs

A `QDialog` closed with Escape is only hidden; shown again after the main
window left and re-entered full screen, it can open on another Space (Qt
forum thread below). Dialogs kept between uses (`newProjectPopup_`…) are
recreated, or their native window destroyed when they close.

### 1.4 The full-screen command

The menu bar is not native (`TitleBar.cpp`: `setNativeMenuBar(false)`):
macOS does not add *Enter Full Screen* (⌃⌘F, fn F).

**Fix:** View ▸ Enter Full Screen, `QKeySequence::FullScreen`, toggling
`showFullScreen()` / `showNormal()`.

---

## 2. One process per project window

`MainWindow::openProjectInNewWindow` (`MainWindowProject.cpp`) starts the
bundle's inner executable with `QProcess::startDetached`. On macOS:

- **One Dock icon per project**; ⌘\` does not cycle between projects;
  "When switching to an application, switch to a Space with open windows"
  treats them as different applications, which adds to the jumps between
  Spaces.
- **Starting the inner executable bypasses LaunchServices**: the new window
  is not sure to come forward. **Fix:** on macOS,
  `open -n -a <Duet.app> --args <folder>`.
- **Long term:** one process with several `MainWindow`s gives one Dock icon,
  ⌘\` and a standard Window menu. A large change, worth it only if the
  icons per project are a problem.

---

## 3. Bundle identity

The built `Info.plist` (CMake's default template):

```
CFBundleIdentifier     => com.yourcompany.duet
CFBundleIconFile       => ""
LSMinimumSystemVersion => ""
NSHumanReadableCopyright => ""
Signature=adhoc, linker-signed, Info.plist=not bound
```

- **Bundle identifier:** a real one, through a `packaging/macos/Info.plist.in`
  (`MACOSX_BUNDLE_INFO_PLIST`). Notifications, Spaces assignment (Dock ▸
  Options ▸ Assign To), privacy permissions (TCC) and the defaults domain
  depend on it.
- **Icon:** `duet.icns` built with `iconutil` from
  `packaging/brand/duet-*.png`, in `packaging/macos/`, set by
  `MACOSX_BUNDLE_ICON_FILE` and copied into `Contents/Resources`. Without it,
  Finder and the Dock (before launch) show the generic icon.
- **Other keys:** `LSApplicationCategoryType =
  public.app-category.developer-tools`, `LSMinimumSystemVersion`,
  `NSHumanReadableCopyright`, and `CFBundleDocumentTypes` declaring
  `public.folder` (and source files) so that Duet appears in Open With.
- **Opening from the Dock or Finder:** a folder dropped on the Dock icon, or
  `open -a Duet <folder>`, arrives as a `QFileOpenEvent`, not in `argv`.
  Duet handles none today. **Fix:** handle `QEvent::FileOpen` in `main.cpp`
  and route it like the command line (`ProjectWindows::raise`, else a new
  window).

---

## 4. Privacy permissions and signing

- `~/Desktop`, `~/Documents` and `~/Downloads` are protected. An ad-hoc
  signature identifies Duet by its code's hash: **each rebuild can bring the
  "access your Desktop folder" prompt back**. The prompt also covers the
  child processes (claude, git, clangd), Duet being their responsible
  process. **Fix:** sign development builds with a stable identity (Apple
  Development certificate, or a local self-signed one) in a post-build
  `codesign`.
- **Distribution:** `macdeployqt` and the hardened runtime
  (`codesign --options runtime`), then notarization. The helpers of
  `Contents/MacOS` (`duet-hook`, `duet-mcp`, `duet-askpass`) are signed too.
- **Downloaded toolchains** (`~/.duet/tools`): remove the
  `com.apple.quarantine` attribute after extraction, or Gatekeeper blocks
  their executables.

---

## 5. Notifications, Dock and App Nap

- **Notifications:** Qt 6.10 still sends `QSystemTrayIcon::showMessage`
  through `NSUserNotification`, deprecated since macOS 11, and shows a status
  item in the menu bar (`MainWindowClaude.cpp`). **Fix:** a
  `NotificationsMacOS.mm` on `UNUserNotificationCenter`; it needs the bundle
  identifier and the signature of §3 and §4.
- **Dock badge:** `QGuiApplication::setBadgeNumber()` (Qt ≥ 6.5) for "Claude
  waits for you"; `QApplication::alert` already bounces the icon.
- **Dock menu:** `QMenu::setAsDockMenu()` with the recent projects and New
  Window.
- **App Nap:** in the background, while Claude works, macOS may slow Duet's
  timers. **Fix:** `NSProcessInfo beginActivityWithOptions:
  NSActivityUserInitiated…` while `AgentActivity` is `Working`, ended
  afterwards. Checked with the App Nap column of Activity Monitor.

---

## 6. Terminal keyboard

On a French Mac keyboard, `{ [ | ~ \` are typed with ⌥. `VtScreen::sendKey`
sends that text with `VTERM_MOD_ALT`, which libvterm turns into an Escape
prefix: the shell or Claude would receive `ESC {` instead of `{` (to
confirm).

**Fix:** on macOS, printable text produced with ⌥ is sent without Alt,
Terminal.app's default; optionally a "Use Option as Meta key" preference,
as Terminal.app and iTerm have.

Optional: Secure Keyboard Entry (`EnableSecureEventInput`) while askpass
asks for a password.

---

## 7. Menus with Duet's own menu bar

- With `setNativeMenuBar(false)`, the macOS menu bar shows the default
  application menu only: no Settings… (⌘,) nor About, since
  `QAction::PreferencesRole` and `AboutRole` only apply to a native menu bar.
  **Fix:** on macOS, a second, parentless `QMenuBar` holding these roles and
  Quit, with the same actions.
- File ▸ Quit closes the window (`QWidget::close`): right with one process
  per window; to revisit with a single process.

---

## Order of work

1. The detached terminal and the debug check of stray windows (Spaces).
2. Bundle identifier, icon, `QFileOpenEvent`.
3. Stable signing (privacy prompts).
4. `open -n` for new windows.
5. ⌥ in the terminal.
6. Native notifications and App Nap.
7. Dock menu and badge.

---

## References

- Qt Cocoa plugin, 6.10:
  [qcocoawindow.mm](https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/cocoa/qcocoawindow.mm?h=6.10),
  [qcocoasystemtrayicon.mm](https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/cocoa/qcocoasystemtrayicon.mm?h=6.10)
- [Qt forum: dialog opens on another desktop in full screen](https://forum.qt.io/topic/131126/dialog-opens-on-another-desktop-when-opened-in-macos-fullscreen-mode/8)
- [Avalonia #9565: child window and full screen](https://github.com/AvaloniaUI/Avalonia/issues/9565)
- [gitify PR #3322: window level and full-screen Spaces](https://github.com/gitify-app/gitify/pull/3322)
- [Apple forums: window visible on all Spaces](https://developer.apple.com/forums/thread/26677)
- [QTBUG-64769: Qt::Tool over full screen](https://bugreports-test.qt.io/browse/QTBUG-64769)
- [Qt blog: expanded client areas and safe areas in Qt 6.9](https://www.qt.io/blog/expanded-client-areas-and-safe-areas-in-qt-6.9)
- [Qt forum: expanded client area in QMainWindow](https://forum.qt.io/topic/162068/expanded-client-area-in-qmainwindow)
- [QFileOpenEvent (Qt 6)](https://doc.qt.io/qt-6/qfileopenevent.html)
- [Qt forum: QTimer and App Nap](https://forum.qt.io/post/213669)
- [Apple forums: App Nap](https://developer.apple.com/forums/thread/691454)
- [Qt Interest: codesign and notarization with Qt 6.9](https://lists.qt-project.org/pipermail/interest/2025-October/040325.html)
- [Apple DTS: Desktop/Documents prompts and the responsible process](https://developer.apple.com/forums/thread/127789)
- [Total Phase: several instances with open -n](https://totalphase.com/support/articles/200349376-Multiple-Application-Instances-on-Mac-OS-X)
- [QTBUG-107008: QSystemTrayIcon on macOS](https://bugreports-test.qt.io/browse/QTBUG-107008)
- [Qt forum: native Mac menu items](https://forum.qt.io/post/411694)
