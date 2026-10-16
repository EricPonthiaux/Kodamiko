# Duet

Open source source code editor for **C, C++, Java and JavaScript/TypeScript**,
with an **integrated Claude Code terminal** and **git integration**: pending
changes, staging by file, by hunk or by line, and commits. It runs on Linux, Windows and macOS (Qt 6).

## Features

- **Accurate syntax highlighting** with [tree-sitter](https://tree-sitter.github.io/)
  for C, C++, Java, JavaScript (JSX), TypeScript and TSX. Parsing is
  incremental: only what changes is re-parsed on each keystroke.
- Highlighting of auxiliary files with QScintilla lexers: JSON, CMake,
  Markdown, XML, HTML, CSS, shell, Python, YAML, Makefile, INI/TOML, SQL, diff.
- **Integrated terminal** (xterm 256-color / truecolor emulation with libvterm):
  a Claude Code session started automatically in the project folder, in a
  tab that cannot be closed, plus regular shells in closable tabs.
- **Diff mode**: pending changes, split as in `git status` into staged and
  unstaged, with their diff, updated after every Claude response and every git
  command. From there you stage or unstage files, hunks or lines, discard
  changes and commit. The diff reads without git's notation: a title bar
  per file, a separator per block ("⋯ lines 40–48 · function"), added lines
  on green and removed ones on red without "+" or "-", and the line numbers
  before and after in the margin (History mode shows its diffs the same way).
- **History mode**: the commits of the current branch, filterable by
  message, author or hash, with their branches and tags; for each one, its
  message, its files and its diff. Commits read on two lines, in the manner
  of GitHub Desktop: the author's avatar, the branches and tags as pills and
  the summary, then the author, the date ("2 days ago") and the short ID.
  Changed files, here and in Diff mode, show their icon, their name, their
  folder dimmed and a colored badge for the change.
- **Safety net**: before discarding changes, the editor keeps a copy of the
  affected files (untracked files included); before Claude works on each
  prompt, a copy of the whole working directory (ignored files excepted).
  *Git ▸ Restore a Snapshot* puts them back, which undoes everything Claude
  changed during that prompt.
- **Claude status at a glance**: a dot on the Claude tab and on the Terminal
  mode tab shows whether Claude is working (amber), waiting for you (blue) or
  has finished while you were elsewhere (green). When the window is in the
  background, the taskbar flashes and a desktop notification appears.
- **Usage in the status bar**: context window used and plan limits (5 hours,
  7 days), with model, session cost and reset times in the tooltip.
- **Command palette** (Ctrl+Shift+P): every menu command, found by typing a
  few letters, with its shortcut.
- **File tree in the manner of IntelliJ's Project view**: names colored by
  their git state (blue modified, green added, red untracked, olive ignored),
  folders colored by the files they contain, an orange dot on the files Claude
  wrote during the last prompt. Typing a few letters starts a speed search
  (↑ ↓ to the other matches). The toolbar chooses the scope (*Project*,
  *Changed Files*, *Edited by Claude*), selects the opened file (Alt+F1, or
  always with *Always Select Opened File*) and collapses everything. The
  context menu copies the path (Ctrl+Shift+C, Ctrl+Alt+Shift+C from the
  project root), sends `@path` to the Claude prompt (Ctrl+Alt+K), opens a
  terminal or the file manager there, and runs git on the file or folder:
  Show Diff, Add, Rollback (after a snapshot), Show History.
  Folders come first, with an icon per file type; a chain of folders that
  hold only one folder takes one row (`src/main/java`), companion files nest
  under their main file (`Foo.h` under `Foo.cpp`, `package-lock.json` under
  `package.json`, `x.js.map` under `x.ts`), and files ignored by git can be
  hidden (toolbar options). Ctrl+X / Ctrl+C / Ctrl+V cut, copy and paste
  files (from a file manager too), F5 copies under a new name, and
  drag and drop moves (Ctrl: copies) after a confirmation. Nothing is ever
  overwritten: a name already taken is asked for again.
- **Change marks in the margin**, as in IntelliJ: the lines that differ from
  the git index (green: added, blue: modified, red triangle: deleted), unsaved
  edits included. A click on a mark shows the previous lines, with *Rollback*
  (undone by Ctrl+Z) and *Show Diff*; Ctrl+Alt+Shift+↓ / ↑ go to the next or
  previous change, Ctrl+Alt+Z rolls back the one at the cursor.
- **Find / Replace** (Ctrl+F / Ctrl+R): a bar above the editor that searches as
  you type, highlights every match and shows "3/12"; match case, whole words
  and regular expressions (`\1` references in the replacement).
- **Go to File, Symbol and Recent Files** (Ctrl+Shift+N, Ctrl+Alt+Shift+N,
  Ctrl+E): one popup, a tab per list (Tab switches), fuzzy matching.
  `name:42` opens a file at line 42. In Recent Files, *Edited by Claude* (or
  Ctrl+E again) lists the files Claude wrote, most recent first.
- The command palette, Go to and Find in Files open as panels over the
  window, centered, with a shadow, sized after the window; drag their edges
  or corners to resize them, and that size is kept. Escape or a click
  elsewhere closes them.
- **Find in Files** (Ctrl+Shift+F): searches the project as you type (files
  ignored by git excepted, unsaved editor text included), with a file mask
  and a preview of the selected match.
- **Status bar**: cursor position and selection size (a click opens Go to
  Line, Ctrl+G), line ending and indentation (a click changes them),
  encoding.
- **Outline panel**: the functions, classes, methods, fields… of the
  displayed file (headings for Markdown, functions and targets for CMake), nested
  and filterable. A click moves the cursor to the symbol; the symbol that
  contains the cursor is selected.
- Automatic reloading of open files when they change on disk.
  If you had unsaved changes, the editor asks before reloading, and Ctrl+Z
  brings your version back.
- Encoding and line endings preserved byte for byte (UTF-8 with or without BOM,
  Latin-1, LF/CRLF).
- **Markdown viewer**: a `.md` file opens rendered exactly as on GitHub:
  GitHub's parser ([cmark-gfm](https://github.com/github/cmark-gfm)), its
  markup (heading anchors, alerts `> [!NOTE]`, task lists, footnotes) and its
  style sheet ([github-markdown-css](https://github.com/sindresorhus/github-markdown-css),
  dark or light after the editor theme), in a web view (Qt WebEngine) where
  page scripts never run; code blocks are highlighted with GitHub's classes; the buttons at the top
  right switch between *Editor*, *Split* (the preview follows the text as you
  type, and the scrolling) and *Preview*, and the choice is kept. Links to
  other files open them in a tab, `#heading` links scroll to the heading,
  relative images are shown.
- Image viewer (PNG, JPEG, GIF, BMP, SVG… depending on the Qt plugins):
  fit to the tab, zoom with Ctrl+wheel, Ctrl++ / Ctrl+- and Ctrl+0.
- Diagram editor for `.flow` files ([Flowboard](#diagrams)), opened in a
  tab like any file: flowcharts with Mermaid import/export and SVG export.
- Three themes, in *View ▸ Theme*: Dark, **Purple** (deep violet in the
  manner of AI tools: plum background, lavender text, pastel syntax colors,
  violet accents) and Light. All three share the same controls: rounded
  fields and menus, focus outlines in the accent color, gradient selections,
  underlined current tab, thin scroll bars.
- **Duet's own window**, in the manner of IntelliJ: one bar at the top
  holds the menus, the project (with the recent folders), Claude's state (a
  click shows the session), Go to File, Find in Files and the command
  palette, and the window controls (the git branch stays in the status bar,
  where a click opens the Git menu); rounded corners
  and a soft shadow in the theme's colors. The system still moves and resizes
  the window (snapping to the screen edges works); a double-click on the bar
  maximizes it. *View ▸ Use System Title Bar* brings the system's frame back
  (tiling window managers); on macOS, the traffic lights stay, over
  Duet's bar.
- **Typography**: the interface is set in [Inter](https://rsms.me/inter/) and the code
  (editor, diffs, terminal) in [JetBrains Mono](https://www.jetbrains.com/lp/mono/)
  without ligatures, both embedded so that Duet looks the same on every
  system; lines of code at 1.5 times the font size, with room between the
  line numbers and the text.

## Building

Requirements:

- **Qt 6.5 or newer** (Widgets, Network, PrintSupport, Test and WebEngine
  modules; Qt WebEngine needs WebChannel and Positioning);
- CMake 3.24 or newer, Ninja, a C++20 compiler (GCC 11+, Clang 14+, MSVC 2022);
- network access during the first configuration.

All other dependencies (QScintilla, tree-sitter and its grammars,
libvterm, libgit2, cmark-gfm) are downloaded and built statically
automatically.

```sh
cmake --preset default        # configure (Release, in build/)
cmake --build --preset default
ctest --preset default        # tests
./build/bin/duet [project-folder]
```

<a id="diagrams"></a>**Diagrams (optional).** The `.flow` diagram editor comes from
Flowboard's sources. Point `FLOWBOARD_SOURCE_DIR` to them when configuring
(FreeType is used if installed); without it, `.flow` files open as text:

```sh
cmake --preset default -DFLOWBOARD_SOURCE_DIR=/path/to/flowboard
```

The diagram tab is saved with Ctrl+S like any file, reloaded when the file
changes on disk, and asks before closing with unsaved changes. Its own menus
(in French) cover layout, display, Mermaid import/export and SVG export.

On Windows, run these commands from a "Developer PowerShell for VS 2022"
after adding Qt's `bin` folder to the `PATH`.

To create an install folder (used by the packages):

```sh
cmake --install build --component duet --prefix <folder>
```

The CI (`.github/workflows/ci.yml`) builds and tests on all three systems,
then produces an AppImage, a Windows zip and a macOS dmg.

## Usage

The window has four full-screen modes, chosen with the tabs at the bottom right:

- **Terminal**: the Claude session (always open) and terminals.
- **Edit**: side panel (**Files** and **Outline**, vertical tabs at the
  window's edge as in KDevelop: clicking the current one folds the panel
  away, clicking it again brings it back) and
  editor tabs.
- **Diff**: the pending changes of the git repository, in two sections:
  **Staged** (what the next commit will contain) and **Unstaged**
  (untracked files included). Clicking a file scrolls to its diff,
  double-clicking opens it in the editor. The view updates after every
  change made by Claude and every git command run in a terminal.
- **History**: the commits of HEAD, from newest to oldest (those
  that touch the open folder, if it is a subfolder of the repository). The
  field at the top filters by message, author, hash or branch. For the
  selected commit: its message, its files and its diff against its first
  parent; clicking a file scrolls to its diff, double-clicking opens it in
  the editor. Right-click on a commit: copy its hash.

### Targeted commits

- **Files**: *Stage*, *Unstage*, *Discard…* buttons, or right-click on
  the selected files (a section header stands for all its files).
- **Hunks and lines**: right-click in the diff. Without a selection, the action
  applies to the hunk under the mouse; with a selection, only to the selected
  `+` and `-` lines.
- **Commit**: message below the list, then *Commit* or Ctrl+Enter. *Amend
  last commit* replaces the last commit (`--amend`) and reuses its
  message. If nothing is staged, the editor offers to stage everything.
- **Discarding** changes resets the files to their state in the index
  (untracked files are deleted). A copy is taken beforehand:
  *Git ▸ Restore a Snapshot* puts it back on disk.

Operations go through the `git` command, which must be installed: the
repository's hooks (pre-commit…), commit signing and your configuration
apply just as in a terminal. A failure shows git's message, including
hook output. The current branch is shown in the status
bar.

| Action | Shortcut |
|---|---|
| Command palette | Ctrl+Shift+P |
| Terminal / Edit / Diff / History mode | Ctrl+1 / Ctrl+2 / Ctrl+3 / Ctrl+4 |
| Switch to the terminal and back | F11 |
| File outline (filter) | Ctrl+Shift+O |
| Find / replace in the file | Ctrl+F / Ctrl+R, then F3 / Shift+F3 |
| Find in files | Ctrl+Shift+F |
| Go to file / symbol / recent files | Ctrl+Shift+N / Ctrl+Alt+Shift+N / Ctrl+E |
| Go to line[:column] | Ctrl+G |
| Select the opened file in the tree | Alt+F1 |
| In the tree: copy the path / the path from the project root | Ctrl+Shift+C / Ctrl+Alt+Shift+C |
| In the tree: send the file to Claude | Ctrl+Alt+K |
| In the tree: cut / copy / paste, copy under a new name | Ctrl+X / Ctrl+C / Ctrl+V, F5 |
| Next / previous change, rollback | Ctrl+Alt+Shift+↓ / Ctrl+Alt+Shift+↑, Ctrl+Alt+Z |
| Refresh the diff | F5 |
| Prepare a commit | Ctrl+Shift+G |
| Commit (in the message) | Ctrl+Enter |
| Open a folder | Ctrl+K, Ctrl+O |
| Claude session (restarts it if it has exited) | Ctrl+Shift+L |
| New terminal | Ctrl+Shift+T |
| Copy / paste in the terminal | Ctrl+Shift+C / Ctrl+Shift+V (Cmd+C / Cmd+V on macOS) |
| Save / save all | Ctrl+S / Ctrl+Alt+S |

The Claude session uses the `claude` command
([Claude Code](https://claude.com/claude-code)) 2.1 or newer, and the Diff
mode uses `git` 2.23 or newer. On a new machine, if either is missing, the
editor opens *Help ▸ Check Prerequisites* at startup: it shows what is
installed, and an **Install** button runs the official installation command
(Claude Code's installer, or your system's package manager for git) in a
terminal tab, where you can follow it and type your password if asked. Once
it finishes, the check runs again and the Claude session starts. On its
first launch, Claude Code asks you to log in to your Claude account.

When the terminal has focus, it keeps the keys terminal programs use:
Ctrl+letter (Ctrl+C, Ctrl+R, Ctrl+E…), Alt+letter, Esc, Shift+Tab and the
function keys. The combinations they do not use reach the application's
shortcuts: Ctrl+Shift+… and Ctrl+Alt+… (Cmd+… on macOS), such as
Ctrl+Shift+P, Ctrl+Shift+N, Ctrl+Shift+F, Ctrl+Shift+T or Ctrl+Alt+S, as
well as Ctrl+1/2/3/4 and F11; Ctrl+Shift+C / Ctrl+Shift+V stay the
terminal's copy and paste. The other editor shortcuts take effect again as
soon as you click in a file.

## How the editor follows Claude

1. The editor launches `claude --settings <file>` with *hooks* on
   `UserPromptSubmit`, `PostToolUse` (editing tools and Bash),
   `PermissionRequest`, `Notification`, `Stop` and `StopFailure`, and with a
   status line. The user's Claude configuration is not modified.
2. Each hook runs `duet-hook`, a small program shipped with the
   editor. It forwards the event to the editor over a local socket, then
   waits for its acknowledgement.
3. On `UserPromptSubmit`, the editor records a snapshot of the whole
   working directory (if it differs from the previous one) before letting
   Claude start; *Claude ▸ Snapshot Before Each Prompt* turns this off.
4. On `PostToolUse`, the editor reloads the modified open files (all of them
   after a Bash command). On `PostToolUse` and `Stop`, it recomputes the Diff
   mode from the project's git repository: changes made by shell commands
   (`sed`, generators…) therefore show up too. Following Claude never
   modifies the repository's branches or index: only your actions in Diff
   mode do.
5. The hooks also drive the status dot: working from the prompt to `Stop`,
   waiting on `PermissionRequest` and on the notifications that need you.
   Claude Code sends no event when you interrupt it, so Esc in the Claude
   terminal resets the dot.
6. The status line runs `duet-hook statusline`, which forwards the
   session metrics to the editor and prints the line shown under Claude's
   prompt. If you configured your own status line, its output is shown
   instead.

## Architecture

```
src/
  app/        main window, wiring of the modules
  ui/         shared UI foundation: theme, fonts, icons, window frame, popups, splitters
  editor/     QScintilla editor, tabs, file reading/writing, disk watching, find bar, change marks
  viewers/    non-text tabs: Markdown preview, images, diagrams
  completion/ code completion: popup, fuzzy matching, snippets, local and LSP providers
  lsp/        language server client and server management
  syntax/     highlighting: tree-sitter (highlights.scm queries), QScintilla lexers
  outline/    Outline panel: outline.scm queries (syntax/queries), Markdown, CMake
  terminal/   PTY (forkpty / ConPTY), VT emulation (libvterm), terminal widget
  claude/     Claude Code launching, hooks, local server
  git/        repository status and history (libgit2), operations (git command), partial patches, snapshots
  changes/    Diff mode: staging, commit, snapshot recovery
  history/    History mode: commits, their files and their diffs
  navigation/ symbol index, Go to Definition, Go to File / Symbol / Recent Files
  search/     Find in Files
  workspace/  file tree, side panel, command palette, project file listing
  projects/   project detection and per-project configuration
  toolchains/ toolchain and runtime detection (JDKs…), installation plans
  welcome/    welcome window, project setup and provisioning
  settings/   preferences and keymap
  setup/      prerequisites check (git, Claude Code) and installation
tools/duet-hook/   Claude Code hooks → editor relay
tests/                 Qt Test tests (ctest)
cmake/                 third-party dependencies
```

Modules depend on interfaces (`ISyntaxHighlighter`, `IOutlineProvider`, `IPty`). Adding a
language or a PTY type is done by adding an implementation, without
modifying the rest.

**Adding a tree-sitter language**: declare the grammar in
`cmake/Dependencies.cmake` and `cmake/TreeSitter.cmake`, add its highlighting
query to `TREE_SITTER_QUERY_FILES`, write its outline query
`src/syntax/queries/<language>/outline.scm` (conventions in the folder's
`README.md`), then register it in `src/syntax/TreeSitterGrammars.cpp` and
`src/syntax/LanguageRegistry.cpp`. For a file without a grammar, the
outline is obtained by implementing `IOutlineProvider` (see
`src/outline/TextOutlines.cpp`) and adding it to `OutlineProviders::create`.

## License

Duet is distributed under the **GNU GPL v3** (see `LICENSE`), the license required
by QScintilla. Third-party components:

| Component | License |
|---|---|
| Qt 6 | LGPL v3 |
| QScintilla | GPL v3 |
| tree-sitter and grammars | MIT |
| libvterm | MIT |
| libgit2 | GPL v2 with linking exception |
| cmark-gfm | BSD 2-Clause |
| github-markdown-css, Octicons (in `third_party/`) | MIT |
| Inter, JetBrains Mono (in `third_party/fonts/`) | SIL Open Font License 1.1 |
