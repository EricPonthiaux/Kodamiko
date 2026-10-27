# Windows integration

What Duet needs to fit in a Windows environment: the frameless window and
Snap, bringing a window forward, the taskbar and notifications, executable
identity, the terminal (ConPTY, keyboard), paths, packaging and signing. Only
the fixes that apply to Duet's architecture (Qt 6 Widgets, one process per
project window, Duet's own frame and title bar) are kept.

Sources: Duet's code (`src/ui/WindowFrame.cpp`, `src/terminal/windows/ConPty.cpp`,
`src/app/MainWindowProject.cpp`, `src/CMakeLists.txt`…), Qt 6.10's Windows
plugin (`qwindowswindow.cpp`, `qwindowscontext.cpp`) and the references at the
end. Items marked *to confirm* are read from the code, not yet seen on a
Windows machine.

---

## 1. The frameless window

### How Duet draws its frame

`WindowFrame::setFrameless` (`src/ui/WindowFrame.cpp`) sets
`Qt::FramelessWindowHint` and `Qt::WA_TranslucentBackground`, then paints its
own shadow in a `shadowWidth` margin and resizes with `startSystemResize`.
On Windows, Qt makes such a window a `WS_POPUP` without `WS_CAPTION` nor
`WS_THICKFRAME`, translucent through DWM. What the user loses:

| Windows behavior | System frame | Duet's frame today |
|---|---|---|
| DWM shadow, Windows 11 rounded corners | yes | Duet's painted shadow, square corners |
| Snap Layouts (hovering the maximize button) | yes | no: Windows does not know where the button is |
| Aero Snap (dragging to an edge, Win+arrows) | yes | through `startSystemMove`, *to confirm* for Win+arrows |
| Resizing from the edge, at the system's width | yes | from Duet's shadow margin |
| Maximized window within the work area | yes | *to confirm* with an auto-hidden taskbar |

Every repaint of a translucent top-level window composes its alpha channel:
on a large 4K window, it costs more than an opaque one.

### 1.1 A frame DWM knows

**Fix (recommended):** keep a real `WS_THICKFRAME | WS_CAPTION` window and
remove the caption in `nativeEvent`, in a `WindowFrameWindows.cpp`:

- `WM_NCCALCSIZE` returning `0` with `wParam = TRUE`: the client area covers
  the whole window, the system draws nothing; when maximized, inset by the
  frame's thickness (`GetSystemMetricsForDpi(SM_CXSIZEFRAME) +
  SM_CXPADDEDBORDER`) so that nothing goes off screen.
- `DwmExtendFrameIntoClientArea` with a one-pixel margin: DWM draws its
  shadow and, on Windows 11, the rounded corners
  (`DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND`).
- `WM_NCHITTEST`: `HTLEFT`…`HTBOTTOMRIGHT` on the edges, `HTCAPTION` on the
  empty parts of `TitleBar`, and **`HTMAXBUTTON` over Duet's maximize
  button**, which is what shows Snap Layouts.
- No `WA_TranslucentBackground` any more on Windows, nor painted shadow:
  `drawsShadow()` returns false there.

Qt 6.9 added `Qt::ExpandedClientAreaHint` for the same need on macOS and
iOS; if a later Qt supports it on Windows with Snap Layouts, it replaces the
native code above.

### 1.2 The system title bar

With *Use the system title bar* (`MainWindow::setSystemTitleBar`,
`MainWindowTitleBarWindows.cpp`), Windows draws a light title bar above a dark
Duet. **Fix:** `QGuiApplication::styleHints()->setColorScheme()` (Qt ≥ 6.8)
following Duet's theme: Qt then sets `DWMWA_USE_IMMERSIVE_DARK_MODE` on every
window, native file dialogs included.

### 1.3 The detached terminal window

`DetachablePage::detach` creates a parentless window: on Windows it gets its
own taskbar button, its own Alt+Tab entry, and stays when the main window is
minimized. That is the intent (a window the main one does not cover); if it
turns out to be noise, an *owned* window (`Qt::Window` with the main window
as parent) has no taskbar button and minimizes with its owner, but always
stays above it.

---

## 2. One process per project window

### 2.1 Bringing a window forward

`ProjectWindows::raise` (`src/projects/ProjectWindowServer.cpp`) asks the
process that has the project open to show its window, over a named pipe;
that process calls `MainWindow::bringToFront` → `activateWindow()`.

Windows refuses `SetForegroundWindow` to a process that is not in the
foreground: **the window only flashes in the taskbar**, it does not come
forward. The process allowed to give the foreground away is the one the user
just started (or the welcome window's).

**Done:** `ProjectWindows::raise` first calls
`WindowForeground::allowOtherProcess()` (`src/projects/WindowForeground.h`),
which is `AllowSetForegroundWindow(ASFW_ANY)` on Windows and nothing
elsewhere. *To confirm* by opening, from a second Duet, a project already
open in a minimized or covered window.

### 2.2 The same folder spelled differently

The pipe's name hashes `QDir::canonicalPath()`. Windows' file system ignores
case: `C:\Code\Duet` and `c:\code\duet` are the same folder. *To confirm:*
that `canonicalPath()` returns the folder's real case on Windows (it does
through `GetFinalPathNameByHandleW` in recent Qt), otherwise two windows open
on one project. The same goes for `MainWindow::openProjectHere`, which
compares `folder_ == QDir(folder).absolutePath()`: on Windows, a comparison
of canonical paths, or `Qt::CaseInsensitive`.

### 2.3 The taskbar

Each process gets its own taskbar button, grouped under Duet's icon since
they share the executable: nothing to do. Ctrl+Tab and the Window menu do
not reach the other projects, as on macOS (see `macosx-integration.md` §2
for the single-process option).

---

## 3. Executable identity

- **Version information** (done): `packaging/windows/duet.rc.in`, configured
  by CMake, holds the icon and a `VERSIONINFO` block (`FileDescription
  "Duet"`, the version from `PROJECT_VERSION`): Task Manager, the
  notifications and the file's properties show Duet, not `duet.exe`.
- **Application manifest** (done, `packaging/windows/duet.manifest`, merged
  by the linker):
  - `longPathAware`: paths beyond 260 characters (`node_modules`, deep
    build trees) work with the registry's `LongPathsEnabled`.
  - `supportedOS` Windows 10/11.
  - DPI awareness stays Qt's (per-monitor v2, set at run time).
  - **Not** `activeCodePage` = `UTF-8`: Duet decodes its child processes'
    output with `QString::fromLocal8Bit` (cmd's `set` after vcvars,
    vswhere, tar…), and those processes keep the system's code page. With
    UTF-8, a `PATH` under `C:\Users\Émilie\` would come out corrupted from
    the MSVC environment. To reconsider once these places decode with the
    child's code page; the helpers have no manifest yet either.
- **AppUserModelID** (done): `DesktopIntegration::identifyApplication()`
  calls `SetCurrentProcessExplicitAppUserModelID(L"Duet.Duet")` at start.
  The installer's Start menu shortcut (§7) must carry the same ID. Toast
  notifications (§4) and the jump list need it.
- **Opening a folder from Explorer:** the installer registers *Open with
  Duet* on folders and on a folder's background
  (`HKCU\Software\Classes\Directory\shell\Duet` and
  `Directory\Background\shell\Duet`, command `"duet.exe" "%V"`). The folder
  arrives in `argv`, which `main.cpp` already handles.

---

## 4. Notifications and the taskbar

- **Notifications:** `MainWindow::notify` (`MainWindowClaude.cpp`) creates a
  `QSystemTrayIcon` and calls `showMessage`: Windows 10/11 turn the balloon
  into a toast, but the icon stays in the notification area until Duet
  exits, and a toast clicked later in the notification center does nothing
  once the icon is gone. **Fix, short term:** hide the tray icon after
  `messageClicked` or the toast's timeout. **Long term:** a
  `NotificationsWindows.cpp` on WinRT's `ToastNotificationManager`; it needs
  the AppUserModelID and the shortcut of §3.
- **Badge:** `QGuiApplication::setBadgeNumber()` (Qt ≥ 6.5) draws an overlay
  on the taskbar button: the same "Claude waits for you" as the Dock badge.
  `QApplication::alert` already flashes the button.
- **Progress:** a build in progress could show on the taskbar button
  (`ITaskbarList3::SetProgressState`), optional.
- **Jump list:** recent projects and New Window on the taskbar button's
  menu. Qt 6 dropped `QWinJumpList` (Qt WinExtras): `ICustomDestinationList`
  in a `JumpListWindows.cpp`. Optional.

---

## 5. Terminal

### 5.1 ConPTY

`ConPty` (`src/terminal/windows/ConPty.cpp`) uses the system's ConPTY
(`kernel32`), the version of the installed Windows: on Windows 10 it lags
years behind Windows Terminal's (resize reflow, sequences passed through,
performance).

- **Option:** ship `conpty.dll` and `OpenConsole.exe` from Microsoft's
  `Microsoft.Windows.Console.ConPTY` package (MIT) next to `duet.exe`, and
  load `CreatePseudoConsole` from it when present, as Windows Terminal and
  VS Code do.
- **Child processes:** `ClosePseudoConsole` ends the programs attached to
  the console, not those that detached from it (a server started by Claude,
  a watcher): they survive the tab. **Fix:** the shell in a Job Object with
  `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, closed with the tab.
- On old versions of ConPTY, `ClosePseudoConsole` waits until the output is
  read: `closeConsole` closes it while the reader thread still reads, which
  is right; to keep that way.

### 5.2 Keyboard

`TerminalWidget::isApplicationShortcut` already treats AltGr (Ctrl+Alt on
Windows) as text. But `VtScreen::sendKey` then receives the modifiers
Ctrl+Alt:

- for a letter (AltGr+E → `€` on a French keyboard, AltGr+A → `ą` in
  Polish), the "Ctrl+letter" branch sends the control code (Ctrl+E, the end
  of the line) instead of the character;
- for a symbol (AltGr+( → `{`), the character goes with Ctrl+Alt, which
  libvterm encodes as an escape sequence.

**Done:** `VtScreen::sendKey` first asks
`KeyModifiers::isComposedByAltGr`: on Windows, Ctrl and Alt both held with
printable text; the text then goes without modifiers
(`tests/windows/tst_terminal_windows.cpp`). *To confirm* on a French
keyboard.

### 5.3 Copy and paste

Copy is Ctrl+Shift+C and paste Ctrl+Shift+V outside macOS. Windows Terminal
copies with Ctrl+C **when text is selected** (otherwise Ctrl+C goes to the
program) and pastes with Ctrl+V; Windows users expect it. **Done:** on
Windows, Ctrl+C with a selection copies it and clears it (the next Ctrl+C
interrupts), Ctrl+V pastes; Ctrl+Shift+C/V stay. Claude Code pastes images
with Alt+V on Windows, so Ctrl+V loses nothing there.

### 5.4 Claude Code and Git Bash

Claude Code on Windows runs its commands in Git Bash (`SystemSetupWindows`
says so). When Git is not in its default place (a portable Git, Scoop),
Claude does not find `bash.exe`: Duet, which knows Git's path, sets
`CLAUDE_CODE_GIT_BASH_PATH` in the session's environment.

---

## 6. Files and paths

- **URIs from the language servers:** clangd and others send
  `file:///c%3A/…` or `file:///c:/…`, with a lowercase drive letter;
  `FilePaths::fromUrlPath` keeps it, and a comparison with Duet's `C:/…`
  fails (diagnostics or references on a file shown as not open). *To
  confirm.* **Fix:** upper-case the drive letter and decode `%3A` in
  `fromUrlPath`.
- **Line endings:** Git for Windows installs with `core.autocrlf = true`:
  files arrive as CRLF. The editor keeps a file's line endings on save, and
  the diff ignores none of them: *to check* with a CRLF file.
- **Locked files:** an antivirus, the indexer or a running program keeps a
  file open, and a rename or a delete fails with *Access denied*
  (`QSaveFile`, snapshots, `rename_file`). The error says which file and
  which program class may hold it; a retry after a short delay covers the
  antivirus.
- **Defender:** the real-time scan slows down `git`, builds and the
  extraction of `~/.duet/tools`. The documentation suggests a Dev Drive or
  an exclusion; Duet sets nothing itself.
- **Downloaded toolchains:** Duet writes the archives itself, without the
  *Mark of the Web* (`Zone.Identifier`): SmartScreen does not block their
  executables, unlike Gatekeeper on macOS. Nothing to do.
- **Settings:** `QSettings` go to `HKCU\Software\Duet\Duet`: nothing to do.

---

## 7. Packaging and signing

### 7.1 What an install lacks today

`windeployqt` runs after the **build** only (`src/CMakeLists.txt`), with
`--no-compiler-runtime`; `cmake --install --component duet` copies
`duet.exe`, the helpers and Graphviz's DLLs, **without Qt's DLLs, plugins,
Qt WebEngine (`QtWebEngineProcess.exe`, its resources) nor the MSVC
runtime**: the installed Duet does not start.

**Fix:** `qt_generate_deploy_app_script` (Qt ≥ 6.3) and `install(SCRIPT …)`
for Qt; `InstallRequiredSystemLibraries` for the MSVC runtime (app-local),
or the VC++ Redistributable in the installer. The helpers (`duet-hook`,
`duet-mcp`, `duet-askpass`) use the same DLLs, next to them.

### 7.2 Installer

- **Per-user**, in `%LOCALAPPDATA%\Programs\Duet`, without elevation:
  Inno Setup or WiX (MSI), or MSIX. MSIX gives a clean uninstall and
  updates, but virtualizes `AppData` and constrains child processes: to try
  with clangd and Claude before choosing it.
- A Start menu shortcut carrying the AppUserModelID of §3, *Open with Duet*
  on folders, an uninstaller that removes them.

### 7.3 Signing

- An unsigned executable shows SmartScreen's *Windows protected your PC*,
  and antivirus software is quick to flag unsigned helpers that launch
  processes and open pipes (`duet-hook`, `duet-askpass`).
- **Fix:** Authenticode for every `.exe` and `.dll` of Duet, the installer
  included (`signtool sign /fd sha256 /tr <timestamp server>`), with an EV
  certificate or Azure Trusted Signing for an immediate SmartScreen
  reputation.

---

## Order of work

1. Installing Qt and the MSVC runtime (§7.1): without it, nothing ships.
2. ~~Bringing a window forward (`AllowSetForegroundWindow`).~~ Done.
3. ~~AltGr in the terminal, then Ctrl+C/Ctrl+V with a selection.~~ Done.
4. ~~Version information, manifest (long paths), AppUserModelID.~~ Done;
   UTF-8 code page left out (§3).
5. The DWM frame: shadow, rounded corners, Snap Layouts.
6. The drive letter in the language servers' URIs, path comparisons.
7. Installer, *Open with Duet*, Authenticode signing.
8. Toast notifications, badge, jump list; the Job Object of the terminals.

---

## References

- Qt Windows plugin, 6.10:
  [qwindowswindow.cpp](https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/windows/qwindowswindow.cpp?h=6.10),
  [qwindowscontext.cpp](https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/windows/qwindowscontext.cpp?h=6.10)
- [Microsoft: support Snap Layouts for custom title bars](https://learn.microsoft.com/windows/apps/desktop/modernize/apply-snap-layout-menu)
- [Microsoft: custom window frame with DWM](https://learn.microsoft.com/windows/win32/dwm/customframe)
- [Microsoft: rounded corners in Windows 11](https://learn.microsoft.com/windows/apps/desktop/modernize/apply-rounded-corners)
- [Microsoft: SetForegroundWindow and its restrictions](https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setforegroundwindow)
- [Microsoft: AllowSetForegroundWindow](https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-allowsetforegroundwindow)
- [Microsoft: application manifests (longPathAware, activeCodePage)](https://learn.microsoft.com/windows/win32/sbscs/application-manifests)
- [Microsoft: use the UTF-8 code page](https://learn.microsoft.com/windows/apps/design/globalizing/use-utf8-code-page)
- [Microsoft: maximum path length limitation](https://learn.microsoft.com/windows/win32/fileio/maximum-file-path-limitation)
- [Microsoft: AppUserModelIDs](https://learn.microsoft.com/windows/win32/shell/appids)
- [Microsoft: creating a pseudoconsole session](https://learn.microsoft.com/windows/console/creating-a-pseudoconsole-session)
- [Microsoft: ClosePseudoConsole](https://learn.microsoft.com/windows/console/closepseudoconsole)
- [microsoft/terminal: the ConPTY package](https://github.com/microsoft/terminal)
- [Microsoft: Job Objects](https://learn.microsoft.com/windows/win32/procthread/job-objects)
- [Qt: QStyleHints::setColorScheme](https://doc.qt.io/qt-6/qstylehints.html#colorScheme-prop)
- [Qt: QGuiApplication::setBadgeNumber](https://doc.qt.io/qt-6/qguiapplication.html#setBadgeNumber)
- [Qt: deploying with CMake (qt_generate_deploy_app_script)](https://doc.qt.io/qt-6/qt-generate-deploy-app-script.html)
- [Qt: Qt for Windows deployment (windeployqt)](https://doc.qt.io/qt-6/windows-deployment.html)
- [Qt blog: expanded client areas and safe areas in Qt 6.9](https://www.qt.io/blog/expanded-client-areas-and-safe-areas-in-qt-6.9)
- [Claude Code: setup on Windows (Git Bash)](https://docs.anthropic.com/en/docs/claude-code/setup)
- [Microsoft: Dev Drive and Defender's performance mode](https://learn.microsoft.com/windows/dev-drive/)
- [Azure Trusted Signing](https://learn.microsoft.com/azure/trusted-signing/)
