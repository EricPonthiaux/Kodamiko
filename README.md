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
  **Generate** (Ctrl+Alt+G, next to *Commit*) asks the Claude session in the
  terminal for the commit message of the changes to commit (everything when
  nothing is staged, the whole commit with *Amend*), in the style of the
  recent commits: the session knows why the changes were made. Claude hands
  the message back with the `set_commit_message` tool, without committing;
  it replaces the text, which Ctrl+Z brings back.
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
- **Claude sees what you see**: with each prompt, the editor tells Claude
  the file in front of you, the cursor, the selected lines and their text,
  the problems underlined in the file and the files with unsaved changes,
  so that "fix this" or "why is this line wrong?" needs no `@path`. Claude
  receives it as context, not as part of your prompt; *Claude ▸ Share
  Editor Context With Each Prompt* turns it off.
- **Ask Claude about the code**: the editor's context menu (and the
  *Claude* menu) offers *Explain This Code*, *Review This Code* and *Write
  Tests for This Code*, sent at once to the Claude session about the
  selected lines, or the word at the cursor without a selection;
  *Add to Claude's Prompt* (Ctrl+Alt+K in the editor) types a mention of
  those lines (`@src/main.cpp#L10-24`) in the prompt for you to complete.
  A right click outside the selection moves the cursor there first.
- **Claude works in the editor**: Duet's own MCP server gives its Claude
  session tools that see what you see and act in the editor. Claude reads
  your open files with their unsaved changes, the selection, the problems
  the language servers report (also underlined in the editor now), symbols,
  definitions and usages, search results, git status and history, snapshots,
  terminals and diagrams (their content and what you selected in them). It
  opens a file at a line, renames a symbol everywhere through the language
  server, applies a quick fix, formats a file, shows a diff, lays out,
  selects in or exports a diagram, triggers a menu command. What changes the
  repository or runs something (restoring a snapshot, staging, committing,
  running a command in a terminal you watch) waits for *Allow* in a bar at
  the top of the window. The read-only tools need no permission; prompts
  such as `/mcp__duet__review-selection`, `/mcp__duet__fix-diagnostics`,
  `/mcp__duet__explain-symbol` and `/mcp__duet__draw-diagram` are offered
  as commands. *Claude ▸ Expose Duet's Tools to Claude* turns it off (from
  the next session).
- **Your unsaved edits are safe from Claude**: when Claude is about to edit
  a file that has unsaved changes in the editor, the editor refuses the edit
  and tells Claude why, so that it asks you to save or discard them instead
  of overwriting them; a bar above the editor names the files, with *Save
  All*.
- **Diagrams**: *File ▸ New Diagram* (or *New Diagram* in the tree's
  context menu) lists the kinds of diagram available (flowchart, sequence,
  class, state, data model, C4 architecture, mind map, user journey,
  product vision, explainer board) and creates a blank one under the name
  you give. *Drawn by Claude…* (also
  *Claude ▸ Draw a Diagram…*) asks for the kind, what it should show ("the
  login flow in src/auth") and the file name: a tab shows Claude at work
  (the files it reads, the time spent, *Cancel*) until the diagram replaces
  it. This runs `claude --print` on its own, outside the session, allowed to
  read the project and to write that one file only.
- **Claude draws diagrams**: ask for a diagram ("draw the order return
  process", "the classes of src/billing", "the login exchanges") and Claude
  writes it in Mermaid, a `.mmd` file (in `docs/diagrams/` unless you name
  one), that opens in the diagram editor, laid out automatically. How to
  write each kind comes from a skill in Duet's own plugin for its session
  (`tools/claude-plugin`, passed with `--plugin-dir`); after each write,
  Duet reads the file as the editor does and sends back to Claude what it
  could not read (an unsupported kind, a statement it does not understand…),
  which Claude fixes at once. When Claude edits a diagram you arranged,
  your arrangement stays: new elements go beside their neighbors.
- **Claude status at a glance**: a dot on the Claude tab and on the Terminal
  mode tab shows whether Claude is working (amber), waiting for you (blue) or
  has finished while you were elsewhere (green). When the window is in the
  background, the taskbar flashes and a desktop notification appears.
- **Claude configuration at hand**: the *Claude* tab of the side panel lists
  everything Claude Code reads for the project, by category (Memory, Rules,
  Skills, Agents, Commands, Output Styles, Settings, MCP Servers), each file
  with its scope: *User* (`~/.claude`), *Project* (shared through git),
  *Local* (not shared), *Folder* (a subfolder's `CLAUDE.md`), *Parent*,
  *Auto* (the memory Claude writes itself) and *Managed* (read-only); the
  hooks Duet passes to its own session are left out. A double-click opens
  the file in the editor; usual files not created yet (`CLAUDE.local.md`,
  `.claude/settings.local.json`, `.mcp.json`…) are listed in italics and
  created on the first double-click. **+** makes a skill, an agent, a command
  or a rule, in the project or for every project, from a template with its
  front matter; *Delete* (or the Delete key) moves an item to the trash, a
  skill with its folder. *Effective Settings* (first under Settings) opens, read-only,
  the settings Claude applies once its files are merged (User, Project,
  Local, Managed): every value followed by the file it comes
  from, and by the values it replaced (`// Local, over User: "sonnet"`); the
  tab follows the files as they change. Settings files are checked as you
  type against [SchemaStore](https://www.schemastore.org/)'s schema of
  Claude Code's settings (embedded, no network): syntax errors underlined in
  red, wrong types, values or keys in amber, with the message when the mouse
  rests on them (`"defaultMode": "sometimes"` → the accepted modes, `"Wirte"`
  in `permissions.allow` → did you mean `Write`?, `"modle"` → did you mean
  `model`?); resting on a key shows its documentation. Skills and agents get
  their front matter checked (a valid `name`, a `description`). *Restart Claude Session* (in the pane's options) starts a new
  session that goes on with the conversation (`--continue`), for the
  settings Claude reads only when it starts.
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
- **Occurrences of a name**: with the cursor on a variable, a function or
  any name, its other occurrences in the file are highlighted, and marked
  along the scroll bar to find the ones off screen. They show at once as the
  same whole word; when the file's language server runs, they become the
  occurrences of that very symbol (not a homonym in another scope, nor the
  word in a comment).
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
- **Diagram editor**, a C++ port of the diagram engine of sidecar (its
  `shape-board` and its families), opened in a tab like any file and drawn
  in the editor's theme. It reads Mermaid (`.mmd`, `.mermaid`) for every
  kind, PlantUML (`.puml`) for class, state and sequence diagrams, explainer
  boards (`.board`, sidecar's JSON scenes) and Flowboard's `.flow` files.
  The file stays in its notation: Duet writes the source back, followed by
  the layout you made (positions, sizes, colors, curves) in comments that
  other Mermaid tools ignore. Layouts come from Graphviz, as in sidecar:
  layered for flowcharts, classes and states, radial for mind maps, lanes
  for sequences, frames as clusters. Beside the diagram, the selection's
  properties (those of its kind: shape, stereotype, relation, cardinalities,
  technology…; its color, its line style) and, with nothing selected, the
  diagram's direction; the **Source** tab shows the diagram's source,
  editable (*Apply*: elements keep their place by id).
  Double-click edits a text (Enter validates, Shift+Enter starts a new line;
  in multi-line texts such as a class's members, Enter is a new line and
  Ctrl+Enter validates) or, in empty space, offers the kind's elements;
  dragging from one of a shape's ports links it (with the kind's relations
  to choose from where it has several), and released in empty space adds
  the next element, linked. Dragging moves shapes (into and out of frames
  where the kind has them; aligned on the others' edges and centers on
  explainer boards, Alt: freely), the corner handle resizes them; dragging
  in empty space or with the middle button moves the view, Shift+drag
  selects a rectangle, the wheel zooms. Right-click for the context menus.
  Ctrl+L lays the diagram out (animated), F fits it to the view, Ctrl+Z /
  Ctrl+Y undo and redo, Ctrl+C / X / V / D copy, cut, paste (elements, or a
  source of the same kind) and duplicate, arrows move the selection
  (Shift: by 10). *More* exports the source, SVG and PNG (in a light,
  printable palette) and shows or hides the grid and the properties. A
  Mermaid file Duet does not read (a pie chart) opens as text.
- Four themes, in *View ▸ Theme*: **Duet**, the default, on the brand's
  colors (the icon's dark slate, its blue as the accent, its white as the
  caret; flat selections, panels a level below the editor and menus a level
  above), Dark, **Purple** (deep violet in the manner of AI tools: plum
  background, lavender text, pastel syntax colors, violet accents) and
  Light. All four share the same controls: rounded fields and menus, focus
  outlines in the accent color, accent selections (a gradient in Dark,
  Purple and Light), underlined current tab, thin scroll bars. The fold
  marks are thin chevrons: those of the folded blocks always show, those of
  the open blocks only while the mouse is over the margin. The caret's line
  number is brighter than the others, and the indentation guide of the
  block holding the caret lights up.
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
- **Typography**: the interface (menus, panels, trees) is set in
  [Ubuntu Sans](https://design.ubuntu.com/font) and the code (editor, diffs,
  terminal) in Ubuntu Sans Mono, both embedded so that Duet looks the same on
  every system; [JetBrains Mono](https://www.jetbrains.com/lp/mono/), embedded
  too, is the other code font offered. Lines of code at 1.5 times the font
  size, with room between the line numbers and the text. *Preferences ▸
  Fonts* sets the code font, its weight (Light, Regular, Medium), the line
  height, programming ligatures in the editor ("->" drawn as an arrow, with
  a font that has them such as JetBrains Mono; the terminal keeps one
  character per cell), and the sizes of the interface and the trees. Geist
  Mono, Commit Mono, Monaspace and Iosevka are offered first when installed,
  with where to get them otherwise. Ligatures need Qt 6.7 for fonts other
  than the embedded ones.

## Building

Requirements:

- **Qt 6.6 or newer** (Widgets, Network, PrintSupport, Test and WebEngine
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

<a id="diagrams"></a>**Diagrams (optional).** The diagram editor lays
diagrams out with [Graphviz](https://graphviz.org)'s library, found with
`pkg-config` (`libgraphviz-dev` on Debian and Ubuntu, `graphviz` with
Homebrew or pacman); its dot and neato engines are linked in. Without it,
diagram files open as text. Qt's Svg module, when present, adds the SVG
export.

The diagram tab is saved with Ctrl+S like any file, reloaded when the file
changes on disk, and asks before closing with unsaved changes.

On Windows, run these commands from a "Developer PowerShell for VS 2022"
after adding Qt's `bin` folder to the `PATH`.

To create an install folder (used by the packages):

```sh
cmake --install build --component duet --prefix <folder>
```

**Code style, formatting and static analysis.** `CLAUDE.md` holds the
conventions (cognitive complexity, naming, ownership, errors…);
`.clang-format` and `.clang-tidy` enforce what tools can, and Duet's code
builds without a compiler warning (`-Wall -Wextra -Wpedantic -Wshadow…`).
With clang-format and clang-tidy (and its `run-clang-tidy` script)
installed:

```sh
cmake --build build --target format        # reformat src/, tests/ and tools/
cmake --build build --target format-check  # fail on an unformatted source
cmake --build build --target tidy          # analyze every source: it reports nothing
cmake --preset debug -DDUET_CLANG_TIDY=ON  # analyze each source as it compiles
git config core.hooksPath tools/git-hooks  # refuse commits of unformatted sources
```

The CI (`.github/workflows/ci.yml`) builds and tests on all three systems,
then produces an AppImage, a Windows zip and a macOS dmg.

## Usage

The window has four full-screen modes, chosen with the tabs at the bottom right:

- **Terminal**: the Claude session (always open) and terminals.
- **Edit**: side panel (**Files**, **Outline** and **Claude**, vertical tabs at the
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
  the editor. Right-click on a commit: copy its hash, or check it out
  without a branch.

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

### Branches and remotes

- **Status bar**: the current branch, with `↑2 ↓1` when it has commits its
  upstream lacks and the reverse (as of the last fetch). Clicking it opens
  the **Branches** popup (Ctrl+Alt+B); the ↻ button next to it syncs.
- **Fetch, Pull, Push, Sync** (Git menu): their progress shows in the
  status bar, with a button to stop them. *Pull* follows your `pull.rebase`
  and `pull.ff` settings; without either it only fast-forwards, and when
  both sides have new commits it offers to rebase or merge. *Push* publishes
  a branch without upstream to `origin`; when the remote rejects it, it
  offers to pull first or to overwrite (`--force-with-lease`). *Sync*
  fetches, pulls if needed, then pushes.
- **Credentials**: when git or ssh needs a password, a key passphrase or
  your word on an unknown host, the editor asks in a dialog (through
  `duet-askpass`, shipped with it). Nothing is stored: your credential
  helper and ssh-agent keep working as usual.
- **Branches popup**: local then remote branches, filtered as you type;
  Enter switches. Switching keeps the uncommitted changes that do not
  conflict, after a snapshot; when they do conflict, the editor offers to
  stash them and bring them back on the other branch. A remote branch gets
  a local branch tracking it. Right-click: merge into the current branch,
  rebase onto it, rename, delete (on the remote too), track.
- **Merge and rebase**: when they stop on conflicts, Diff mode shows a bar
  with the number of conflicts and *Continue*, *Skip Commit* (rebase) and
  *Abort*; stage each file once resolved. The same actions are in the Git
  menu.
- **Stashes**: *Git ▸ Stash Changes…* puts the changes aside (new files
  included unless unchecked); *Git ▸ Stashes…* lists them with their diff,
  to *Pop*, *Apply* or *Drop*.

Operations go through the `git` command, which must be installed: the
repository's hooks (pre-commit…), commit signing and your configuration
apply just as in a terminal. A failure shows git's message, including
hook output.

| Action | Shortcut |
|---|---|
| Command palette | Ctrl+Shift+P |
| Branches / Pull / Push | Ctrl+Alt+B / Ctrl+Alt+U / Ctrl+Alt+P |
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
| In the editor: add the selected lines to Claude's prompt | Ctrl+Alt+K |
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
   `UserPromptSubmit`, `PreToolUse` (editing tools), `PostToolUse` (editing tools and Bash),
   `PermissionRequest`, `Notification`, `Stop` and `StopFailure`, and with a
   status line. The user's Claude configuration is not modified.
2. Each hook runs `duet-hook`, a small program shipped with the
   editor. It forwards the event to the editor over a local socket, then
   waits for its acknowledgement, which may carry an answer for Claude
   Code: `duet-hook` prints it, as hooks do, and prints nothing else.
3. On `UserPromptSubmit`, the editor records a snapshot of the whole
   working directory (if it differs from the previous one) before letting
   Claude start; *Claude ▸ Snapshot Before Each Prompt* turns this off.
   Its answer adds the editor context (open file, cursor, selection,
   problems, unsaved files) to the prompt.
   On `PostToolUse` of a diagram file, its answer lists the problems the
   diagram editor found in it, and the diagram opens in a tab.
   On `PreToolUse`, its answer refuses an edit of a file with unsaved
   changes in the editor, with the reason for Claude.
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
7. The session is also given Duet's MCP server (`--mcp-config`):
   `duet-mcp`, another small program shipped with the editor, which Claude
   Code runs and talks MCP with over its standard input and output. It
   relays each message to the editor over the same local socket (a
   connection that stays open), where the tools, resources and prompts are
   served; without an editor to reach, it answers as a server with no
   tools. The read-only tools are allowed in the session's settings; the
   others follow Claude Code's permissions, and the sensitive ones also
   wait for the editor's *Allow*.

## Architecture

```
src/
  app/        main window, wiring of the modules (MainWindow*.cpp, one file per area: menus, run, git, Claude…)
  ui/         shared UI foundation: theme, fonts, icons, window frame, popups, splitters
  editor/     QScintilla editor, tabs, file reading/writing, disk watching, find bar, change marks, Markdown preview
  viewers/    non-text tabs: images, diagrams (diagram/: model, Graphviz layout, canvas, files; families/: one per kind of diagram)
  completion/ code completion: popup, fuzzy matching, snippets with tab stops, postfix and live templates, local and LSP providers, smart and extended completion, Full Line, doc comments, override, getter and setter generation, name suggestions, context-aware keywords, module paths in JavaScript imports
  lsp/        language server client and server management
  syntax/     highlighting: tree-sitter (highlights.scm queries), QScintilla lexers
  outline/    Outline panel: outline.scm queries (syntax/queries), Markdown, CMake
  terminal/   PTY (forkpty / ConPTY), VT emulation (libvterm), terminal widget
  claude/     Claude Code launching, hooks, local server; mcp/: the editor's MCP server and its tools
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
tools/duet-mcp/    Claude Code's MCP server → editor relay
tools/duet-askpass/ git and ssh credential prompts → editor relay
tools/claude-plugin/ Duet's plugin for its Claude session: skills (diagrams)
tools/git-hooks/   pre-commit check of the formatting
tests/                 Qt Test tests (ctest)
cmake/                 third-party dependencies, warnings, format and tidy targets
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
| Ubuntu Sans, Ubuntu Sans Mono (in `third_party/fonts/`) | Ubuntu Font Licence 1.0 |
| JetBrains Mono (in `third_party/fonts/`) | SIL Open Font License 1.1 |
