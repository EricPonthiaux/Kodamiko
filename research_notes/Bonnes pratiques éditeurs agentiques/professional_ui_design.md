# Professional UI Design for a Desktop Code Editor (Qt 6 Widgets application to Enlumine)

Scope note: research done on 2026-10-15 with about 16 search and fetch calls. Primary sources are JetBrains IntelliJ Platform UI guidelines (jetbrains.design/intui now redirects to plugins.jetbrains.com/docs/intellij/ui-guidelines-welcome.html), VS Code UX guidelines, the Linear redesign write-up, the Superhuman command-palette article, the KDAB "Say No to Qt Style Sheets" article, Qt blog posts, and the Qlementine repository. Items marked "(general knowledge, not verified this session)" in Inferences come from prior knowledge and must be checked before they are stated as fact.

## 1. Design choices behind the polish of Zed, Sublime, JetBrains New UI, VS Code, Linear, Warp, Ghostty

### Takeaway
The editors people call polished share a few habits. They cut visual noise and reveal advanced features progressively (JetBrains New UI). They keep separate UI, editor and terminal fonts with generous line height (Zed). They pair a neutral UI sans with a purpose-built monospace (Inter with JetBrains Mono). They generate themes from a few perceptual-color tokens rather than hundreds of hand-picked colors (Linear). They make every action reachable from a keyboard command palette. And they make speed perceptible: Zed's marketing and reputation rest on 120 fps rendering.

### Cited Findings
**JetBrains New UI (default since 2024.2)**
- Rationale: reduce visual complexity, give easy access to essential features, and disclose complex functionality progressively. Key changes were a simplified main toolbar, a new tool-window layout, new themes and updated icons — [Rider help: New UI](https://jetbrains.com/help/rider/2025.1/New_UI.html)
- Work started in 2021. The team released Preview and Beta builds and ran more than 10 UX studies. Stated goals were "bigger, easier-to-use controls, a consistent color palette, light and legible icons, increased contrast, and better accents" — [JetBrains blog, Jul 2024](https://blog.jetbrains.com/blog/2024/07/08/the-new-ui-becomes-the-default-in-2024-2/)
- 87% of users adopted the new UI before it became the default. Critics called it a "VS Code lookalike" and complained it takes more space than the old dense UI. A Classic UI plugin remains available, which shows density is a real trade-off and users value a compact mode — [DevClass](https://www.devclass.com/development/2024/08/06/jetbrains-updates-ides-with-new-ui-by-default-now-a-vs-code-lookalike-some-devs-think-so/1626147)

**Typography defaults**
- JetBrains IDEs use the system UI font by default: macOS SF Pro Text 13, Windows Segoe UI 12, Linux Ubuntu 15 (as listed) — [IntelliJ Platform UI guidelines: Typography](https://plugins.jetbrains.com/docs/intellij/typography.html)
- JetBrains Mono's recommended size is 13 px with a relative line spacing of 1.2 — [OMG! Ubuntu on JetBrains Mono](https://www.omgubuntu.co.uk/2020/01/jetbrains-mono-font-for-developers)
- Font rendering of the same point size varies across OS, scaling and hinting. JetBrains staff said they changed hinting and that results depend on OS, display scaling and zoom — [JetBrains support thread](https://intellij-support.jetbrains.com/hc/zh-cn/community/posts/13760074947474/comments/14051207106962)
- Zed has separate font settings for the editor (buffer), the UI and the terminal. Its docs' example pairs JetBrains Mono (editor) with Inter (UI). Buffer line height offers "comfortable" (1.618, the default), "standard" (1.3) or a custom value — [Zed docs: Appearance](https://zed.dev/docs/appearance)
- GitLab picked Inter as its primary UI typeface, with disambiguation features enabled, and JetBrains Mono for code. This is the same pairing, adopted independently — [GitLab typography epic](https://gitlab.com/groups/gitlab-org/-/epics/8972)
- Linear uses Inter Display for headings and Inter for everything else — [Linear: How we redesigned the Linear UI](https://linear.app/now/how-we-redesigned-the-linear-ui)

**Color and theme systems**
- Linear moved theme generation from HSL to LCH, a perceptually uniform color space. Each theme went from 98 variables to 3 inputs: base color, accent color and contrast. The contrast input can produce high-contrast accessibility themes, and LCH also derives elevation levels (background, panels, dialogs, modals). Linear limited how much the brand blue tints the chrome, to keep it neutral and timeless. Text and neutral icons were made darker in light mode and lighter in dark mode for content contrast — [Linear redesign](https://linear.app/now/how-we-redesigned-the-linear-ui)
- Linear also reduced visual noise in the sidebar, tabs, headers and panels, and aligned labels, icons and buttons on both axes. It used Apple standards for spacing — [Linear redesign](https://linear.app/now/how-we-redesigned-the-linear-ui)
- Zed ships icon themes, selectable from the command palette, that restyle file and folder icons in the project panel and tabs — [Zed docs: Appearance](https://zed.dev/docs/appearance)

**Performance perception**
- Zed built its own GPU UI framework (GPUI). Its blog posts include "Leveraging Rust and the GPU to render user interfaces at 120 FPS" (Mar 2023) and "Optimizing the Metal pipeline to maintain 120 FPS in GPUI" (Feb 2024) — [Zed blog, performance tag](https://zed.dev/blog/tagged/performance)
- Third-party reviews repeat claims of a 0.4 s startup (versus 3.0 s for VS Code) and 2 ms input latency, but give no methodology. Treat these as unverified — [gilricardo.com review](https://www.gilricardo.com/blog/zed-editor-review-2026); [logicity.in](https://logicity.in/en/blog/zed-vs-vs-code-the-rust-editor-that-feels-instant)

**Command palette and keyboard-first design**
- Superhuman defines the palette as a free-form input that narrows a list of commands as you type. Because it is not limited by screen space, it lets you add features without adding buttons or clutter. It also teaches users each command's shortcut "for next time" — [Superhuman blog](https://blog.superhuman.com/how-to-build-a-remarkable-command-palette/)
- Practitioner guides recommend Cmd/Ctrl+K (in editors Ctrl+Shift+P is also standard), inline shortcut hints, and the palette as the escape hatch that finds every action. Linear pairs it with mnemonic single-key shortcuts — [blakecrosley.com Linear guide](https://blakecrosley.com/guides/design/linear); [command palette spec](https://cdn.jsdelivr.net/npm/@hegemonart/get-design-done@1.60.4/reference/components/command-palette.md) (community sources, lower authority)

### Inferences
- For Enlumine, a sensible default typography is the system UI font, or bundled Inter, at about 13 px (9.75–10 pt) for UI, with JetBrains Mono at 13–14 px and line height 1.2–1.4 for the editor and terminal. Let users choose the three fonts separately, as Zed does.
- Build the theme from a small token set (base, accent, contrast) and derive surfaces with a perceptual space (OKLCH or LCH) rather than hand-tuning dozens of QPalette and QSS colors. This keeps dark and light themes consistent.
- A command palette over every QAction, with fuzzy matching, recent commands first and shortcuts shown on the right, gives the biggest "professional" signal per line of code in a keyboard-first editor.
- (general knowledge, not verified this session) Other traits often credited for polish:
  - Sublime Text: near-instant startup, Goto Anything, minimal chrome.
  - Nova: native macOS feel.
  - Warp: block-based terminal output.
  - Ghostty: native platform UI per OS, GPU rendering, sane defaults with zero config.
  - Popular themes: One Dark, Catppuccin (four flavors: Latte, Frappé, Macchiato, Mocha), Tokyo Night, GitHub themes, Solarized.
  - Shipping Catppuccin-style light and dark pairs is a common expectation.
- Animation restraint: animate only state changes that need continuity, such as panel expand or tab drag, in under about 150–200 ms. Never block input on an animation. (Inference; no primary source fetched.)

### Gaps
- I did not fetch Zed's design blog content (only titles), Warp or Ghostty design write-ups, Nova, or Sublime design statements, so their specific choices are uncited.
- I found no authoritative source for the exact UI font size of JetBrains New UI or of VS Code (13 px UI and 14 px editor is common knowledge, not verified here).
- Onboarding and empty-state guidance beyond VS Code's mention of Walkthroughs and Welcome Views was not researched in depth.

## 2. Documented guidelines relevant to dev tools (JetBrains, VS Code, HIGs)

### Takeaway
JetBrains publishes the most concrete numbers for a dev tool: 16 px icons with 14 px live area, 2 px strokes, a five-color action palette, and left-aligned labels. VS Code's guidelines are mostly restraint rules: few notifications, no custom colors in the status bar, progress shown in place rather than in toasts.

### Cited Findings
**JetBrains icon guidelines**
- Sizes:
  - 16 px for toolbar and tree icons, with artwork inside 14×14 and a 1 px transparent border.
  - 12 px for the gutter and status bar.
  - 13 px for tool windows.
  - 32 px for dialogs (2 px border).
  - — [IntelliJ icons style](https://plugins.jetbrains.com/docs/intellij/icons-style.html)
- Geometry: flat and geometric, preferred angles 30/45/60/90°, minimal anchor points. Main stroke is 2 px, pixel-aligned (or 0.5 px when whole pixels are impossible). Base shapes: square 12×12, circle 14 px diameter. Modifiers are 6–9 px in the bottom-right with a 1–2 px gap — [IntelliJ icons style](https://plugins.jetbrains.com/docs/intellij/icons-style.html)
- Action icon palette (light / dark) — [IntelliJ icons style](https://plugins.jetbrains.com/docs/intellij/icons-style.html):

  | Color | Light | Dark | Meaning |
  |---|---|---|---|
  | Grey | #6E6E6E | #AFB1B3 | Default |
  | Red | #DB5860 | #C75450 | Destructive |
  | Yellow | #EDA200 | #F0A732 | Warning |
  | Green | #59A869 | #499C54 | Positive |
  | Blue | #389FD6 | #3592C4 | Accent |

- Noun (file-type) icons use 60% opacity on large areas, 70% on medium and 100% on small details. No gradients or shadows. Never rely on color alone. The dark variant is named `iconName_dark.svg` and is only needed when the icon actually differs — [IntelliJ icons style](https://plugins.jetbrains.com/docs/intellij/icons-style.html)

**JetBrains layout guidelines**
- Always left-align labels.
- Use vertical insets to separate groups, and keep horizontal insets smaller than the vertical ones so related controls read as a group.
- Place dependent controls closer to their parent than to unrelated neighbors.
- Put 2–3 checkboxes with short labels on one line. Put four or more in 2 columns when labels are up to 30 characters, or 3 columns when labels are up to 15.
- Give multiple lists in one dialog the same width.
- — [IntelliJ layout](https://plugins.jetbrains.com/docs/intellij/layout.html)

**VS Code UX guidelines**
- Architecture:
  - Activity Bar, Primary and Secondary Sidebar, Editor, Panel (terminal, problems, output), Status Bar.
  - Status bar: left side is workspace-scoped, right side is file-scoped.
  - Common elements: Command Palette, Quick Pick, Notifications, Context Menus, Walkthroughs for onboarding, Welcome Views for empty states.
  - — [VS Code UX overview](https://code.visualstudio.com/api/ux-guidelines/overview)
- Status bar:
  - Keep labels short and use icons only when their meaning is clear.
  - Don't add custom colors, more than one icon or more than one item unless necessary.
  - Error and warning backgrounds are a last resort.
  - — [VS Code status bar guidelines](https://code.visualstudio.com/api/ux-guidelines/status-bar)
- Notifications:
  - Use them only when truly necessary, and show one at a time.
  - Always offer "Do not show again".
  - Never repeat them, never use them for promotion, never ask for feedback on first install, and add no buttons that aren't needed.
  - Progress belongs in the relevant view. Progress notifications are a last resort, should show stages, offer cancel and link to logs, and must never run indefinitely.
  - — [VS Code notifications guidelines](https://code.visualstudio.com/api/ux-guidelines/notifications)

### Inferences
- Enlumine's four full-screen modes map onto JetBrains' "progressive disclosure" idea. Each mode should show only its own chrome, and the status bar should follow VS Code's left (workspace: git branch, Claude session state) versus right (file: line/col, encoding, EOL, language) split.
- Adopt the JetBrains 16 px / 2 px-stroke icon grid, or a line icon set that follows it (e.g. Lucide, Codicons, Tabler — general knowledge, licenses to verify). Recolor monochrome icons from theme tokens instead of shipping per-theme files.
- Apple HIG, GNOME HIG and Fluent were not fetched. Generally relevant points (general knowledge, not verified this session):
  - GNOME HIG: header bars, sentence case, an 8 px spacing grid where widgets sit 6/12/18 px apart.
  - Apple HIG: native shortcuts such as Cmd+, for settings.
  - Fluent: layered elevation, 4 px base spacing.

### Gaps
- No direct fetch of the Apple HIG, GNOME HIG or Fluent pages, so platform-HIG numbers are unverified.
- The JetBrains layout page has no pixel spacing values. A "Groups of controls" page or the Int UI Figma kit may have them, but I did not fetch either.

## 3. Qt-specific techniques for modern polish in Qt 6 Widgets

### Takeaway
For a large, theme-switching editor, the more robust route is Fusion (or a QProxyStyle or custom QStyle over it) driven by a token-generated QPalette, with QSS limited to a few leaf widgets. Qlementine (MIT, Qt 6.8+) is a ready-made modern QStyle. Frameless custom title bars are possible through QWindow::startSystemMove/Resize or QWindowKit, but they cost native snapping and behavior, so keep native decorations by default.

### Cited Findings
**QSS versus QStyle**
- Per KDAB, stylesheets break palette-based theming. `setPalette` and `setFont` stop working on styled widgets, which no longer follow system color changes. Selectors are not re-evaluated when dynamic properties change, so you have to re-set the sheet, which is costly. Each `setStyleSheet` re-parses the sheet, and reparenting clears the rule cache. One commenter reports several milliseconds of polish per widget in large, frequently rebuilt panels. Some properties are impossible to set at all — [KDAB: Say No to Qt Style Sheets](https://kdab.com/say-no-to-qt-style-sheets)
- KDAB recommends starting with a QProxyStyle over the native style. For a uniform cross-platform look, base it on QCommonStyle or Fusion and split per-widget code into helper classes. Render SVGs once with QSvgRenderer into cached QPixmaps rather than on every paint — [KDAB](https://kdab.com/say-no-to-qt-style-sheets)
- Counterpoints: QStyle code is harder to write, QSS iterates faster with no recompile, and some teams report no performance problems. The QStyle docs say stylesheets are not supported on custom QStyle subclasses, though David Faure suggests light mixing works — [KDAB comments](https://kdab.com/say-no-to-qt-style-sheets)
- Fusion plus an adjusted QPalette gives a full dark theme without any stylesheet — [medo64 blog](https://medo64.com/2020/08/dark-mode-for-qt-application)
- QProxyStyle can override `drawControl`, `drawComplexControl`, `pixelMetric`, `sizeFromContents`, `styleHint`, `polish` and `standardIcon` — [Qt docs: QProxyStyle](https://doc.qt.io/qt/qproxystyle.html)

**Ready-made modern style**
- Qlementine is a "Modern QStyle for desktop Qt6 applications". It is MIT-licensed, a drop-in `QlementineStyle` (library or style plugin), adds missing widgets such as Switch, and requires Qt 6.8+ — [GitHub oclero/qlementine](https://github.com/oclero/qlementine)

**Dark and light, following the system**
- Qt 6.5 added `QStyleHints::colorScheme` (`Qt::ColorScheme::Light`/`Dark`) and the `colorSchemeChanged` signal. Fusion switches to a dark palette automatically on Windows 11 dark mode — [Qt blog: Dark mode on Windows 11 with Qt 6.5](https://www.qt.io/blog/dark-mode-on-windows-11-with-qt-6.5)
- When the signal fires, the palette may not be updated yet. A Qt maintainer advises handling `QEvent::ApplicationPaletteChange` and then reading `colorScheme()`. Qt 6.8 added `QStyleHints::setColorScheme`, but one user saw no visible effect because it only forwards a request to the platform theme — [Qt forum thread](https://forum.qt.io/topic/163228/switching-between-a-light-and-dark-color-scheme)
- Since 6.5, apps built on Fusion can unexpectedly turn dark when the OS is in dark mode (QTBUG-112657). An app that owns its theme should set its palette explicitly — [QTBUG-112657](https://bugreports-test.qt.io/browse/QTBUG-112657)

**Custom title bars and frameless windows**
- Qt 5.15 added `QWindow::startSystemMove` and `startSystemResize`, which hand moving and resizing to the window manager so snapping and tiling work. Move works on Wayland, X11, macOS and Windows. Resize does not work on macOS, so check the return value and provide a fallback — [Qt blog: Custom window decorations](https://qt.io/blog/custom-window-decorations)
- Reported problems:
  - Aero Snap does not work on frameless windows on Windows ([QTBUG-84466](https://bugreports-test.qt.io/browse/QTBUG-84466)).
  - Odd app-wide mouse-event behavior after these calls ([QTBUG-91077](https://bugreports-test.qt.io/browse/QTBUG-91077)).
- QWindowKit replaces the native title bar while keeping move, resize, the system menu and Windows Snap Layouts — [QWindowKit wiki](https://github.com/stdware/qwindowkit/wiki)

### Inferences
- Recommended stack for Enlumine:
  1. Set Fusion, or Qlementine if Qt 6.8+ is acceptable.
  2. Wrap it in a QProxyStyle for metrics such as tab height, splitter width, scrollbar thickness and focus rings.
  3. Generate the palette from theme tokens and set it on `qApp`.
  4. Keep QSS to a few leaf widgets that cannot be styled otherwise.
  5. Share the same tokens with QScintilla lexer colors and libvterm's 16/256-color palette, so the editor, terminal and chrome match exactly.
- Icons: ship monochrome SVGs. At load time, recolor them to the palette's text color (for example by replacing `currentColor` or a placeholder fill, then rendering through QSvgRenderer into a QIcon at devicePixelRatio). Re-render on palette change. Cache the pixmaps.
- High DPI (Qt 6 default; general knowledge, not verified this session): keep the default `Qt::HighDpiScaleFactorRoundingPolicy::PassThrough`. Use SVG or @2x assets, never raw bitmaps. Test at 125% and 150% for fractional-scaling artifacts such as 1 px lines that blur or vanish. Use device-independent pixel metrics.
- Smooth scrolling: QScintilla does not animate scrolling natively. Pixel-precise touchpad scrolling (`QWheelEvent::pixelDelta`) is the main "feels native" factor on macOS and Wayland. (Inference; QScintilla behavior not verified this session.)
- Keep native window decorations by default. Optionally offer a unified title bar later via QWindowKit. A half-working frameless window (no snap, broken Wayland resize, no shadow) looks more amateur than a native title bar.
- Polished Qt apps to study (general knowledge, not verified this session):
  - Qt Creator: themes defined as token files covering palette, icons and editor colors.
  - KDE Breeze-based apps such as Kate and KDevelop.
  - Telegram Desktop: fully custom-painted widgets with its own style system, not QSS.
  - OBS: QSS themes, which show the maintenance cost of QSS at scale.

### Gaps
- No fetched source on Qt Creator's theme file format, Telegram Desktop's rendering approach, or OBS theming lessons.
- No verified source on QScintilla smooth scrolling or font-rendering settings (e.g. `QFont::PreferNoHinting` or subpixel AA on Linux).
- Qlementine's theming details (JSON themes, auto icon color, animations) are not described on the repo landing page; see oclero.github.io/qlementine.

## 4. Common mistakes that make apps look amateur

### Takeaway
Most "amateur" signals are inconsistencies:
- Mixed icon styles and sizes, and off-grid spacing.
- Hard-coded colors that ignore the theme, and too many saturated accent colors.
- Noisy status bars and notifications.
- Chrome competing with content.
- Laggy input or a slow startup.

### Cited Findings
- Custom colors and many icons in the status bar, repeated notifications, unnecessary buttons, and endless progress toasts are explicitly discouraged — [VS Code status bar](https://code.visualstudio.com/api/ux-guidelines/status-bar); [VS Code notifications](https://code.visualstudio.com/api/ux-guidelines/notifications)
- Gradients, shadows, inconsistent stroke weights, non-pixel-aligned strokes and color-only meaning are discouraged for icons — [IntelliJ icons style](https://plugins.jetbrains.com/docs/intellij/icons-style.html)
- Labels that are not left-aligned, and lists of uneven widths in one dialog — [IntelliJ layout](https://plugins.jetbrains.com/docs/intellij/layout.html)
- Mixing stylesheets with palette theming causes widgets that ignore theme switches, and the QSS look varies by widget — [KDAB](https://kdab.com/say-no-to-qt-style-sheets)
- Brand colors leaking into chrome, and too many hand-maintained color variables, produced inconsistency; Linear cut 98 variables to 3 — [Linear redesign](https://linear.app/now/how-we-redesigned-the-linear-ui)
- Visual clutter and a toolbar full of rarely used buttons are what JetBrains' redesign removed — [JetBrains blog](https://blog.jetbrains.com/blog/2024/07/08/the-new-ui-becomes-the-default-in-2024-2/)

### Inferences
- Further typical amateur tells in Qt apps (inference and general knowledge):
  - The default Qt Fusion look left untouched: 3D-ish scrollbars, the blue-on-white selection in dark themes.
  - QSplitter handles that are too thick or invisible.
  - Mixed fonts or sizes across panels.
  - Dialogs using OK/Cancel order that is wrong for the platform; use QDialogButtonBox to fix it.
  - Missing keyboard focus indicators.
  - Missing tooltips with shortcuts.
  - Blurry icons at fractional scaling.
  - Flicker on theme switch.
  - Inconsistent capitalization; pick title case for menus and sentence case elsewhere, per platform.
  - Unpadded text touching borders.
  - Empty panels with no empty state (e.g. "No changes. Working tree clean.").
  - Modal dialogs for things that could be inline.
- A checklist for Enlumine:
  - One 4 px spacing scale.
  - One icon set at 16 px.
  - One accent color plus semantic red, yellow and green.
  - Theme tokens shared by the chrome, QScintilla and libvterm.
  - A command palette with shortcut hints.
  - A quiet status bar.
  - Empty states for every view.
  - Mode tabs (bottom right) with keyboard shortcuts (e.g. Ctrl+1..4) shown in their tooltips.
  - Startup measured and kept low; lazy-load tree-sitter grammars and git status.

### Gaps
- No user-research or HN-thread source fetched that lists amateur tells for desktop dev tools specifically. The list above is synthesized inference.
