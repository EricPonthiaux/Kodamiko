# Evolution

## Studies

- Full refactoring capability (what is a full refactoring capability)
- Full "find references" capability (what is a full references capability)

Both are answered by the refactoring work below.

## Refactoring as powerful as IntelliJ's, fast on large code bases

Goal: IntelliJ's power (preview, conflicts, comments and strings, undo
everywhere, a wide catalogue) without its slowness on large projects
(indexes on the JVM heap, serial resolution, work on the UI thread).

Design, hybrid: Duet keeps its own native, incremental, persistent index
to find candidates in milliseconds; the language servers (clangd, jdtls,
typescript-language-server, pyright) decide the semantics; Duet alone
handles the preview, the conflicts, the transaction and the undo.
Target: about 1 M lines. Rules: nothing on the UI thread beyond 16 ms,
everything cancellable, parallel on all cores, only what changed read
again.

### Step 0: LSP foundations — done

- Requests can be cancelled (`$/cancelRequest`) and given a timeout.
- Work done progress: the status bar shows what the servers are busy with
  ("clangd: indexing 42%").
- Workspace edits keep their order, document versions and file
  operations (create, rename, delete); an edit computed for an older
  version of a file is refused; `didClose` is sent.
- `prepareRename` before renaming; Go to Implementation
  (Ctrl+Alt+Shift+B) and Go to Type Declaration (Ctrl+Shift+B).
- A scripted fake language server tests the client without clangd.

### Step 1: preview, transaction, undo — done

- Every refactoring is all or nothing: an edit journal reverts what was
  made when a change fails.
- Edit ▸ Undo Refactoring (Ctrl+Alt+Shift+Z) reverts the last
  refactoring in every file it changed, open or not, unless one changed
  since.
- Refactoring Preview: the files changed, the diff of each, files with
  text edits only can be unchecked; Rename offers Preview (Alt+P) next to
  Refactor.

### Step 2: occurrence index — done

- `src/index`: every word of the project with its role (definition,
  reference, call, write, import, comment, string, text), from
  tree-sitter queries for C, C++, Java, JavaScript, TypeScript and Python
  (grammar added for the index only).
- Parsed on all cores but one, encoded in a few bytes per occurrence,
  saved in the cache folder and loaded back at startup.
- `tools/bench-index`: Qt's headers (834k lines) index in 1.3 s cold,
  0.1 s warm; a word's files in under a millisecond. `/usr/include`
  (10.5M lines): 10 s cold, 1.2 s warm.

### Step 3: comments and strings, conflicts, usages — done

- Rename's "Search in comments and strings" also replaces the name in
  comments, strings and non-code files.
- Conflicts: the new name already declared, read-only files, files
  outside the project, a server still reading the project. With textual
  occurrences or conflicts, the preview opens first, as in IntelliJ.
- Usages pane (Alt+3): Find Usages by file with each usage's kind,
  filters, rerun, history, Next/Previous Usage (Ctrl+Alt+Down/Up);
  without a server, every occurrence of the word from the index.
- Hierarchy pane (Alt+8): Call Hierarchy (Ctrl+Alt+H) and Type Hierarchy
  (Ctrl+H), unfolded one level at a time.

### Step 4: Refactor This, file moves, Move Class — done

- Moving or renaming a file or folder in the tree updates C and C++
  includes, JavaScript and TypeScript relative imports and Python
  imports (and the module's uses), as one undoable refactoring.
- Edit ▸ Refactor This (Ctrl+Alt+Shift+T): Duet's refactorings, then the
  server's "refactor" code actions, each shown in the preview.
- Move Class to Its Own File (F6), for C++ (header and members'
  definitions, namespaces, guard style, Qt macros, statics, C typedef
  structs) and Java.
- The editor's context menu starts with the refactorings, the
  navigation, then the agent's submenu.

### Step 5: Change Signature, Claude's refactorings — done

- Change Signature (Ctrl+F6): add, remove, reorder parameters in the
  declarations and at every call, for C, C++, Java, JavaScript,
  TypeScript and Python (self, keyword arguments); the places it cannot
  change are listed as conflicts.
- MCP tools go through the preview and answer once the user chose:
  `rename_symbol` (preview, comments and strings), `apply_code_action`
  (preview), and the new `find_usages` (kinds), `rename_file` and
  `change_signature`.

### Tests

- `tests/refactoring`: a corpus of 116 cases (before, expected after,
  undo checked) over C, C++, JavaScript, TypeScript and Python — file
  moves, Move Class, comments and strings, renames, Change Signature and
  Find Usages through the real servers. Run by `tst_refactorcorpus`.
- Unit tests in `tst_lsp`, `tst_index`, `tst_refactor`,
  `tst_codeintelligence`, `tst_navigation`, `tst_mcptools`.

### Known limits

- Python relative imports (`from . import mod`) and TypeScript ESM
  imports written `./x.js` for `x.ts` are not updated by file moves.
- Find in Files does not use the index yet: keywords are not indexed.
- `SymbolIndex` still parses on its own, beside the occurrence index.
- Change Signature writes a parameter list on one line, and reaches only
  the places the server returns (not overrides).
- The "new name already declared" conflict is checked per file, not per
  scope.

## Trust: in the project, the editor, Claude and the code

Goal: the user can open any folder, let Claude work and commit without
fear: nothing from an unknown folder runs on its own, no work is ever
lost, everything Claude did is visible and reversible, and the state of
the code is known before the commit.

Design: Duet already has the pieces (git snapshots before each prompt,
atomic saves, confirmations for the sensitive MCP tools, problems,
builds, format on save); the work is to close the holes between them and
to show what they know. Rules: a folder is restricted until the user
trusts it; every write by Claude goes through a confirmation, a preview
or a snapshot; a loss of work is a bug.

### Step 0: close the holes — to do

- Folder trust: a folder opens restricted until the user trusts it (once
  per folder, remembered); restricted, Duet runs nothing that comes from
  it: no `node_modules/.bin/prettier` (`editor/CodeFormatter.cpp`), no
  venv `python` given to pyright or pylsp, no Gradle or Maven import by
  jdtls, no `.duet/recipes/` override, no `statusLine` command from
  `.claude/settings*.json` run by `duet-hook`. Claude does not start on
  its own in an untrusted folder, nor in `$HOME`.
- `run_command` refuses the Git menu's actions and the Claude menu's
  settings (Snapshot Before Each Prompt, Expose Duet's Tools); today only
  Quit, Exit, Session, Restart and Close All are refused
  (`agent/mcp/WorkspaceTools.cpp`).
- `rename_file` asks for confirmation, as the sensitive tools do.
- After "Keep My Changes", saving over a file changed on disk asks again
  instead of overwriting it silently.
- A file deleted on disk marks its tab modified, so that closing it asks
  before its content is lost.

### Step 1: never lose work — to do

- Unsaved buffers are copied to the cache folder a few seconds after each
  change, and offered back after a crash or a kill.
- Local history: each save of a file is kept (size- and age-capped),
  with or without git, and compared or restored from the file's menu.
- Closing the window warns when Claude is answering or a run, a build or
  a terminal command is still going.
- A snapshot before dropping a stash, amending a commit or deleting a
  worktree with its changes.
- The session's tab list is saved as it changes, not only on close.

### Step 2: see what Claude did — to do

- A journal per turn: the prompt, the files changed, the commands run
  (`PostToolUse` on Bash keeps the command), the permissions asked and
  the answers.
- The diff of a turn and of any snapshot; Restore a Snapshot shows it
  before restoring.
- The lines Claude changed in the last turn are marked in the gutter
  until the user reviews them.
- Undo a turn file by file, from its diff.
- A worktree agent's work is merged after the user approves its diff,
  not on its own at the end of each response.

### Step 3: the health of the code — to do

- The files Claude changes are sent to their language server even when
  closed, so their problems reach the Problems pane and the tree.
- A permanent indicator: errors, the last build and the last tests, with
  the commit they ran on.
- The commit panel shows the problems of the staged files, a failed
  build or tests, and unformatted files, before the commit.
- The check command of Check Before Merging Agents can also run after
  each turn of the main session, and its failure goes back to Claude.

### Step 4: tests — to do

- The tests of CTest, pytest, Jest, Cargo, Go and Maven listed in a
  pane, run one, a file or all, with a result per test.
- Run the tests affected by the current changes.

### Known risks, not planned

- The hook socket (`agent/HookServer.cpp`) is reachable by any process of
  the user that knows its name (given to Claude's children through
  `DUET_SOCKET`); it has no token.
- The HTML preview runs the page's JavaScript; the Markdown preview loads
  remote images.
- Snapshots run the git binary, which honours the repository's
  `core.fsmonitor`, filters and hooks.
